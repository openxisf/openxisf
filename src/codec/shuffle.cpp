// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "codec/shuffle.h"

#include <openxisf/error.h>

#include <algorithm>

// SSE2 is part of every x86-64 processor, so its kernels need no runtime dispatch. ARM64EC defines _M_X64 too, but
// emulates SSE.
#if defined(OPENXISF_ENABLE_SIMD) && (defined(__SSE2__) || defined(_M_X64)) && !defined(_M_ARM64EC)
#define OPENXISF_SHUFFLE_SSE2
#include <emmintrin.h>
#endif

namespace openxisf::detail {

namespace {

void check_sizes(std::span<const std::byte> input, std::span<std::byte> output)
{
    if (input.size() != output.size()) {
        throw usage_error(errc::invalid_argument, "byte shuffling needs an output of the size of its input");
    }
}

// Byte j of item i is at i * item_size + j in the unshuffled order, and at j * items + i in the shuffled one.

// A part of run j of a shuffled block: run[k] = from[k * item_size], where from starts at byte j of the first item.
void gather_scalar(std::span<const std::byte> from, std::size_t item_size, std::span<std::byte> run) noexcept
{
    for (std::size_t k = 0; k < run.size(); ++k) {
        run[k] = from[k * item_size];
    }
}

// The items [first, end) of a block of items, unshuffled from the runs of input into output.
void scatter_scalar(std::span<const std::byte> input, std::size_t items, std::size_t item_size, std::size_t first,
                    std::size_t end, std::span<std::byte> output) noexcept
{
    for (std::size_t j = 0; j < item_size; ++j) {
        for (std::size_t i = first; i < end; ++i) {
            output[(i * item_size) + j] = input[(j * items) + i];
        }
    }
}

#if defined(OPENXISF_SHUFFLE_SSE2)

// The kernels move 16 items at a time, as many as a register holds bytes.
constexpr std::size_t lanes = 16;

__m128i load(const std::byte* address) noexcept
{
    return _mm_loadu_si128(reinterpret_cast<const __m128i*>(address));
}

void store(std::byte* address, __m128i value) noexcept
{
    _mm_storeu_si128(reinterpret_cast<__m128i*>(address), value);
}

// The first byte of each of the 16 items of Size bytes at items, packed into one register: the bytes above it are
// cleared in each item, and the items are packed with saturation, which leaves values below 256 unchanged.
template <std::size_t Size> __m128i first_bytes(const std::byte* items) noexcept
{
    if constexpr (Size == 2) {
        const __m128i mask = _mm_set1_epi16(0xFF);
        return _mm_packus_epi16(_mm_and_si128(load(items), mask), _mm_and_si128(load(items + 16), mask));
    } else if constexpr (Size == 4) {
        const __m128i mask = _mm_set1_epi32(0xFF);
        const auto pack = [&](std::size_t at) {
            return _mm_packs_epi32(_mm_and_si128(load(items + at), mask), _mm_and_si128(load(items + at + 16), mask));
        };
        return _mm_packus_epi16(pack(0), pack(32));
    } else {
        static_assert(Size == 8);
        const __m128i mask = _mm_set1_epi64x(0xFF);
        // The two items of a register, as its two lower 32-bit lanes.
        const auto pair = [&](std::size_t at) {
            return _mm_shuffle_epi32(_mm_and_si128(load(items + at), mask), _MM_SHUFFLE(2, 0, 2, 0));
        };
        const auto pack = [&](std::size_t at) {
            return _mm_packs_epi32(_mm_unpacklo_epi64(pair(at), pair(at + 16)),
                                   _mm_unpacklo_epi64(pair(at + 32), pair(at + 48)));
        };
        return _mm_packus_epi16(pack(0), pack(64));
    }
}

// gather_scalar() for items of Size bytes, 16 at a time while the loads stay within from.
template <std::size_t Size> void gather_sse2(std::span<const std::byte> from, std::span<std::byte> run) noexcept
{
    std::size_t k = 0;
    for (; k + lanes <= run.size() && (k + lanes) * Size <= from.size(); k += lanes) {
        store(run.data() + k, first_bytes<Size>(from.data() + (k * Size)));
    }
    gather_scalar(from.subspan(k * Size), Size, run.subspan(k));
}

// Interleaves 16 items from the runs, whose first bytes are at run, run + items, run + 2 × items and so on, into the
// 16 items at out: unpacking pairs of runs gives items of 2 bytes, pairs of those items of 4 bytes, and pairs of those
// items of 8 bytes. The low halves of the unpacked registers hold items 0 to 7, the high halves items 8 to 15.
template <std::size_t Size> void interleave(const std::byte* run, std::size_t items, std::byte* out) noexcept
{
    const auto load_run = [run, items](std::size_t j) { return load(run + (j * items)); };
    if constexpr (Size == 2) {
        const __m128i bytes0 = load_run(0);
        const __m128i bytes1 = load_run(1);
        store(out, _mm_unpacklo_epi8(bytes0, bytes1));
        store(out + 16, _mm_unpackhi_epi8(bytes0, bytes1));
    } else if constexpr (Size == 4) {
        const __m128i bytes0 = load_run(0);
        const __m128i bytes1 = load_run(1);
        const __m128i bytes2 = load_run(2);
        const __m128i bytes3 = load_run(3);
        const __m128i low01 = _mm_unpacklo_epi8(bytes0, bytes1);
        const __m128i high01 = _mm_unpackhi_epi8(bytes0, bytes1);
        const __m128i low23 = _mm_unpacklo_epi8(bytes2, bytes3);
        const __m128i high23 = _mm_unpackhi_epi8(bytes2, bytes3);
        store(out, _mm_unpacklo_epi16(low01, low23));
        store(out + 16, _mm_unpackhi_epi16(low01, low23));
        store(out + 32, _mm_unpacklo_epi16(high01, high23));
        store(out + 48, _mm_unpackhi_epi16(high01, high23));
    } else {
        static_assert(Size == 8);
        const __m128i bytes0 = load_run(0);
        const __m128i bytes1 = load_run(1);
        const __m128i bytes2 = load_run(2);
        const __m128i bytes3 = load_run(3);
        const __m128i bytes4 = load_run(4);
        const __m128i bytes5 = load_run(5);
        const __m128i bytes6 = load_run(6);
        const __m128i bytes7 = load_run(7);
        // The 8 items of a half from its pairs of bytes: bytes 0 to 3 of items 0 to 3 and of items 4 to 7, then bytes
        // 4 to 7 of the same items.
        const auto store_half = [](__m128i bytes01, __m128i bytes23, __m128i bytes45, __m128i bytes67,
                                   std::byte* half) {
            const __m128i start0 = _mm_unpacklo_epi16(bytes01, bytes23);
            const __m128i start4 = _mm_unpackhi_epi16(bytes01, bytes23);
            const __m128i end0 = _mm_unpacklo_epi16(bytes45, bytes67);
            const __m128i end4 = _mm_unpackhi_epi16(bytes45, bytes67);
            store(half, _mm_unpacklo_epi32(start0, end0));
            store(half + 16, _mm_unpackhi_epi32(start0, end0));
            store(half + 32, _mm_unpacklo_epi32(start4, end4));
            store(half + 48, _mm_unpackhi_epi32(start4, end4));
        };
        store_half(_mm_unpacklo_epi8(bytes0, bytes1), _mm_unpacklo_epi8(bytes2, bytes3),
                   _mm_unpacklo_epi8(bytes4, bytes5), _mm_unpacklo_epi8(bytes6, bytes7), out);
        store_half(_mm_unpackhi_epi8(bytes0, bytes1), _mm_unpackhi_epi8(bytes2, bytes3),
                   _mm_unpackhi_epi8(bytes4, bytes5), _mm_unpackhi_epi8(bytes6, bytes7), out + 64);
    }
}

// scatter_scalar() of every item, for items of Size bytes, 16 at a time.
template <std::size_t Size>
void scatter_sse2(std::span<const std::byte> input, std::size_t items, std::span<std::byte> output) noexcept
{
    std::size_t i = 0;
    for (; i + lanes <= items; i += lanes) {
        interleave<Size>(input.data() + i, items, output.data() + (i * Size));
    }
    scatter_scalar(input, items, Size, i, items, output);
}

#endif

void gather(std::span<const std::byte> from, std::size_t item_size, std::span<std::byte> run,
            shuffle_kernel kernel) noexcept
{
#if defined(OPENXISF_SHUFFLE_SSE2)
    if (kernel == shuffle_kernel::simd) {
        switch (item_size) {
        case 2:
            gather_sse2<2>(from, run);
            return;
        case 4:
            gather_sse2<4>(from, run);
            return;
        case 8:
            gather_sse2<8>(from, run);
            return;
        default:
            break;
        }
    }
#else
    (void)kernel;
#endif
    gather_scalar(from, item_size, run);
}

void scatter(std::span<const std::byte> input, std::size_t items, std::size_t item_size, std::span<std::byte> output,
             shuffle_kernel kernel) noexcept
{
#if defined(OPENXISF_SHUFFLE_SSE2)
    if (kernel == shuffle_kernel::simd) {
        switch (item_size) {
        case 2:
            scatter_sse2<2>(input, items, output);
            return;
        case 4:
            scatter_sse2<4>(input, items, output);
            return;
        case 8:
            scatter_sse2<8>(input, items, output);
            return;
        default:
            break;
        }
    }
#else
    (void)kernel;
#endif
    scatter_scalar(input, items, item_size, 0, items, output);
}

} // namespace

bool has_simd_shuffle() noexcept
{
#if defined(OPENXISF_SHUFFLE_SSE2)
    return true;
#else
    return false;
#endif
}

void shuffle_bytes(std::span<const std::byte> input, std::span<std::byte> output, std::size_t item_size,
                   shuffle_kernel kernel)
{
    check_sizes(input, output);
    shuffle_part(input, item_size, 0, output, kernel);
}

void shuffle_part(std::span<const std::byte> input, std::size_t item_size, std::size_t offset,
                  std::span<std::byte> output, shuffle_kernel kernel)
{
    if (offset > input.size() || output.size() > input.size() - offset) {
        throw usage_error(errc::invalid_argument, "a part of a shuffled block must lie within the block");
    }
    const std::size_t items = item_size == 0 ? 0 : input.size() / item_size;
    if (item_size <= 1 || items == 0) {
        std::ranges::copy(input.subspan(offset, output.size()), output.begin());
        return;
    }
    // Run j of the shuffled block holds byte j of every item; the part covers pieces of one or more runs.
    const std::size_t end = offset + output.size();
    const std::size_t shuffled_end = items * item_size;
    std::size_t position = offset;
    while (position < end && position < shuffled_end) {
        const std::size_t j = position / items;
        const std::size_t first = position % items;
        const std::size_t count = std::min(end, (j + 1) * items) - position;
        gather(input.subspan((first * item_size) + j), item_size, output.subspan(position - offset, count), kernel);
        position += count;
    }
    if (position < end) {
        std::ranges::copy(input.subspan(position, end - position),
                          output.begin() + static_cast<std::ptrdiff_t>(position - offset));
    }
}

void unshuffle_bytes(std::span<const std::byte> input, std::span<std::byte> output, std::size_t item_size,
                     shuffle_kernel kernel)
{
    check_sizes(input, output);
    const std::size_t items = item_size == 0 ? 0 : input.size() / item_size;
    if (item_size <= 1 || items == 0) {
        std::ranges::copy(input, output.begin());
        return;
    }
    scatter(input, items, item_size, output, kernel);
    std::ranges::copy(input.subspan(items * item_size),
                      output.begin() + static_cast<std::ptrdiff_t>(items * item_size));
}

} // namespace openxisf::detail
