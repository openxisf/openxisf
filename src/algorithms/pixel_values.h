// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <openxisf/image.h>
#include <openxisf/types.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

// The pixel data that the algorithms of the specification read and write in place: samples of a real format, as
// values normalized from the representable range of their image to [0, 1] (spec §8.5.5, equation [4]).

namespace openxisf::detail {

/// The representable range of image, for an algorithm over its pixel data, after checking that the image has real
/// samples, finite bounds in increasing order whose width is finite too when it has bounds, and pixel data of
/// pixel_bytes bytes. Throws usage_error with errc::invalid_argument; what names the algorithm in the message.
[[nodiscard]] bounds checked_range(const image_info& image, std::size_t pixel_bytes, std::string_view what);

/// Equation [4] with a device range of [0, 1]: value mapped from range, clipped.
[[nodiscard]] inline double normalize(double value, const bounds& range) noexcept
{
    if (value < range.lower) {
        return 0.0;
    }
    if (value > range.upper) {
        return 1.0;
    }
    return (value - range.lower) / (range.upper - range.lower);
}

/// The value of range that value, in [0, 1], stands for: the inverse of normalize().
[[nodiscard]] inline double denormalize(double value, const bounds& range) noexcept
{
    return range.lower + (value * (range.upper - range.lower));
}

/// value as a sample of type T: rounded to the nearest integer and clipped to the range of an unsigned integer type, or
/// clipped to the finite range of float. NaN becomes zero for integers.
template <typename T> [[nodiscard]] T to_sample(double value) noexcept
{
    if constexpr (std::is_integral_v<T>) {
        // The largest value of every unsigned type as a double is at most 2^64, which no smaller double rounds to.
        constexpr auto largest = static_cast<double>(std::numeric_limits<T>::max());
        if (!(value > 0.0)) {
            return 0;
        }
        if (value >= largest) {
            return std::numeric_limits<T>::max();
        }
        return static_cast<T>(std::round(value));
    } else if constexpr (std::is_same_v<T, float>) {
        constexpr auto largest = static_cast<double>(std::numeric_limits<float>::max());
        return static_cast<float>(std::clamp(value, -largest, largest));
    } else {
        return value;
    }
}

/// The sample at index of data as a double. data holds samples of type T in native byte order, not necessarily
/// aligned.
template <typename T> [[nodiscard]] double load_value(std::span<const std::byte> data, std::size_t index) noexcept
{
    T sample{};
    std::memcpy(&sample, data.subspan(index * sizeof(T), sizeof(T)).data(), sizeof(T));
    if constexpr (std::is_same_v<T, double>) {
        return sample;
    } else {
        return static_cast<double>(sample);
    }
}

/// Stores value at index of data, as to_sample() makes it a sample of type T.
template <typename T> void store_value(std::span<std::byte> data, std::size_t index, double value) noexcept
{
    const T sample = to_sample<T>(value);
    std::memcpy(data.subspan(index * sizeof(T), sizeof(T)).data(), &sample, sizeof(T));
}

/// The index of the sample of a channel of a pixel, in either storage model (spec §8.5.3).
[[nodiscard]] inline std::size_t sample_index(pixel_storage storage, std::size_t pixel, std::size_t channel,
                                              std::size_t pixel_count, std::size_t channels) noexcept
{
    return storage == pixel_storage::planar ? (channel * pixel_count) + pixel : (pixel * channels) + channel;
}

/// Calls function with std::type_identity<T>, T being the C++ type of the samples of a real format. It does nothing for
/// a complex format, which checked_range() refuses.
template <typename Function> void visit_real_samples(sample_format format, Function&& function)
{
    switch (format) {
    case sample_format::uint8:
        std::forward<Function>(function)(std::type_identity<std::uint8_t>{});
        break;
    case sample_format::uint16:
        std::forward<Function>(function)(std::type_identity<std::uint16_t>{});
        break;
    case sample_format::uint32:
        std::forward<Function>(function)(std::type_identity<std::uint32_t>{});
        break;
    case sample_format::uint64:
        std::forward<Function>(function)(std::type_identity<std::uint64_t>{});
        break;
    case sample_format::float32:
        std::forward<Function>(function)(std::type_identity<float>{});
        break;
    case sample_format::float64:
        std::forward<Function>(function)(std::type_identity<double>{});
        break;
    case sample_format::complex32:
    case sample_format::complex64:
        break;
    }
}

} // namespace openxisf::detail
