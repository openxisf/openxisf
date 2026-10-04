// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <openxisf/error.h>
#include <openxisf/export.h>
#include <openxisf/image.h>
#include <openxisf/io.h>
#include <openxisf/limits.h>
#include <openxisf/property.h>
#include <openxisf/types.h>

#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

/// @file
/// Reading XISF units.

namespace openxisf {

/// How a unit is stored (spec §9.1).
enum class unit_storage
{
    monolithic,  ///< A monolithic file (`.xisf`): the header and the attached data blocks in one file.
    distributed, ///< A header file (`.xish`), whose data blocks are external.
};

/// Whether a unit has an XML signature (spec §9.5).
enum class signature_status
{
    none,         ///< The unit is not signed.
    not_verified, ///< The unit is signed. OpenXISF does not verify signatures.
};

/// Options for opening a unit.
struct read_options
{
    /// Fail with the error of the first diagnostic of severity::error, instead of opening the unit with that object
    /// unavailable. The exception tells the kind of error: integrity_error for data that fail their checksum,
    /// unsupported_error for a feature that OpenXISF does not support, such as an unknown compression codec, and
    /// invalid_data_error for anything else.
    bool strict = false;
    /// Read the header alone: leave the ancillary data in data blocks unloaded, so that nothing but the header is read.
    /// ICC profiles and the pixels of thumbnails stay empty, and the properties and tables with values in data blocks
    /// are left out, until reader::load_ancillary_data() loads them; the problems of those data are found then.
    bool header_only = false;
    /// Safety limits for untrusted input.
    openxisf::limits limits{};
};

/// Options for reading the pixel data of an image.
struct pixel_read_options
{
    /// The storage model of the samples read: that of the image when empty (spec §8.5.3).
    std::optional<openxisf::pixel_storage> storage{};
    /// Called as the data are read and decompressed, and able to cancel the read. Its units of work are the bytes read
    /// from the source plus, for a compressed image, the bytes decompressed.
    progress_function progress{};
};

#if defined(_MSC_VER)
// The reader holds a standard-library member, which cl reports for an exported class. A C++ interface requires the
// same standard library on both sides of the DLL boundary anyway.
#pragma warning(push)
#pragma warning(disable : 4251)
#endif

/// An XISF unit opened for reading.
///
/// The source is recognized by its content: a monolithic file (spec §9.2), or a header file (spec §9.3), which starts
/// with XML. Opening reads and checks the whole header, including where each data block is and how it is encoded, and
/// verifies the checksums of the blocks written in the header; attached blocks are verified when they are read. It
/// describes each image and what is associated with it, and loads the ancillary data in data blocks, within
/// limits::max_ancillary_data: ICC profiles, the pixels of thumbnails, and the values of properties and table cells
/// (read_options::header_only defers them). Pixel data are read on request, with read_pixels().
/// Problems confined to one object of the unit do not fail it: the object is unavailable, and a diagnostic says why
/// (spec §7).
///
/// The reader owns its source, so a file stays open as long as its reader exists. Its const member functions can be
/// called from any number of threads at once, pixel reads included; load_ancillary_data(), the only function that
/// changes it, needs exclusive access. It can be moved, and a moved-from reader can only be destroyed or assigned to.
class OPENXISF_API reader
{
public:
    /// Opens the file at path, which is UTF-8 on every platform.
    /// @throws usage_error when path is empty or not valid UTF-8.
    /// @throws io_error when the file cannot be opened or read.
    /// @throws invalid_data_error when the file is not an XISF unit, or its header violates the specification in a way
    ///         that leaves the unit without meaning.
    /// @throws unsupported_error when the unit is of an XISF version other than 1.0.
    /// @throws limit_error when the header exceeds one of the limits of options.
    /// @throws integrity_error, unsupported_error or invalid_data_error with strict options, for the first error
    ///         diagnostic (see read_options::strict).
    explicit reader(std::string_view path, read_options options = {});

    /// Opens the unit in source, with the same exceptions as the other constructor. Exceptions of the source pass
    /// through unchanged.
    /// @throws usage_error when source is null.
    explicit reader(std::unique_ptr<input_source> source, read_options options = {});

    reader(const reader&) = delete;
    reader& operator=(const reader&) = delete;
    reader(reader&& other) noexcept;
    reader& operator=(reader&& other) noexcept;
    ~reader();

    /// How the unit is stored.
    [[nodiscard]] unit_storage storage() const noexcept;

    /// Whether the unit is signed.
    [[nodiscard]] signature_status signature() const noexcept;

    /// The properties of the unit: those of its Metadata element, such as XISF:CreationTime (spec §11.4).
    [[nodiscard]] const property_list& metadata() const noexcept;

    /// The standalone properties: the Property elements of the root element (spec §11.1).
    [[nodiscard]] const property_list& properties() const noexcept;

    /// The standalone table properties: the Table elements of the root element (spec §11.3), in document order.
    [[nodiscard]] std::span<const table> tables() const noexcept;

    /// The images of the unit (spec §11.5), in document order: each Image element, and the image that each Reference
    /// element of the root element names, listed again where the Reference is (spec §11.13). An Image element whose
    /// attributes cannot be read is left out, with an error diagnostic. An image is listed even when its data block is
    /// unavailable, for example compressed with a codec that OpenXISF does not support; read_pixels() then throws the
    /// error of that diagnostic.
    [[nodiscard]] std::span<const image_info> images() const noexcept;

    /// The image at index in images().
    /// @throws usage_error when index is out of range.
    [[nodiscard]] const image_info& image(std::size_t index) const;

    /// Reads the pixel data of the image at index: its samples in native byte order, in the storage model of
    /// options.storage, or in that of the image. The data are verified against their checksum and decompressed first.
    /// @throws usage_error when index is out of range.
    /// @throws integrity_error when the data fail their checksum or do not decompress to their size.
    /// @throws unsupported_error, invalid_data_error or limit_error when the data block of the image is unavailable, or
    ///         larger than limits::max_allocation, with the code of the error diagnostic about it.
    /// @throws cancelled_error when options.progress returns false.
    /// @throws io_error, or what a custom source throws, when the source fails.
    [[nodiscard]] std::vector<std::byte> read_pixels(std::size_t index, const pixel_read_options& options = {}) const;

    /// Reads the pixel data of the image at index into destination, which must have exactly image(index).data_size()
    /// bytes, with the exceptions of the other read_pixels(). When it throws, destination may hold part of the data.
    /// @throws usage_error when destination has another size.
    void read_pixels(std::size_t index, std::span<std::byte> destination, const pixel_read_options& options = {}) const;

    /// Reads the samples of the image at index as values of T, which must be the type of its sample format
    /// (sample_format_of<T>()): std::uint16_t for a UInt16 image, std::complex<float> for a Complex32 image.
    /// @throws usage_error when T is another type; and what the other read_pixels() throw.
    template <pixel_sample T>
    [[nodiscard]] std::vector<T> read_pixels(std::size_t index, const pixel_read_options& options = {}) const
    {
        std::vector<T> samples(sample_count_of(index, sample_format_of<T>()));
        read_pixels(index, std::as_writable_bytes(std::span(samples)), options);
        return samples;
    }

    /// Reads the samples of the image at index into destination, which must hold exactly its number of samples, as
    /// values of T, the type of its sample format.
    /// @throws usage_error when T is another type or destination has another size; and what the other read_pixels()
    ///         throw.
    template <pixel_sample T>
    void read_pixels(std::size_t index, std::span<T> destination, const pixel_read_options& options = {}) const
    {
        (void)sample_count_of(index, sample_format_of<T>());
        read_pixels(index, std::as_writable_bytes(destination), options);
    }

    /// The problems found while opening the unit, in the order they were found.
    [[nodiscard]] std::span<const diagnostic> diagnostics() const noexcept;

    /// True unless the unit was opened with read_options::header_only and load_ancillary_data() has not been called.
    [[nodiscard]] bool ancillary_data_loaded() const noexcept;

    /// Loads what read_options::header_only left out: the reader then holds what an open without it gives, its
    /// diagnostics included. It reads the unit from its source again, so it fails like an open, with the options of the
    /// open; the reader is unchanged when it throws. It does nothing when the data are loaded.
    ///
    /// It changes the reader, so no other function may be called on it at the same time, and the references, pointers
    /// and spans that the reader returned before are invalid afterwards.
    void load_ancillary_data();

private:
    // The number of samples of the image at index, after checking that it has the given sample format and that its
    // pixel data fit in limits::max_allocation, so that a typed read can allocate them.
    [[nodiscard]] std::size_t sample_count_of(std::size_t index, sample_format format) const;

    struct state;
    std::unique_ptr<state> state_;
};

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

} // namespace openxisf
