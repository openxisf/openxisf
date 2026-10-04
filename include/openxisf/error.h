// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <openxisf/export.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

/// @file
/// Exceptions, error codes and diagnostics.

namespace openxisf {

/// Why an operation failed. The exception type says what kind of failure it is, and this code says why.
/// Callers branch on either, never on the message text. Zero is not a code.
enum class errc
{
    invalid_argument = 1,    ///< A precondition of the API was violated.
    cancelled,               ///< A progress callback asked to stop.
    entropy_unavailable,     ///< The operating system provided no random data.
    arithmetic_overflow,     ///< A size or offset computation does not fit its type.
    invalid_utf8,            ///< Text is not well-formed UTF-8, or contains U+0000.
    invalid_integer,         ///< Text is not an integer in the form of spec §8.3.1 or §8.3.2.
    invalid_float,           ///< Text is not a floating point value in the form of spec §8.3.3.
    invalid_boolean,         ///< Text is not a Boolean value in the form of spec §8.3.4.
    value_out_of_range,      ///< A well-formed value that its declared type cannot represent.
    invalid_base64,          ///< Text is not valid Base64 data.
    invalid_hex,             ///< Text is not valid hexadecimal (Base16) data.
    invalid_uuid,            ///< Text is not a UUID in canonical form.
    invalid_utf16,           ///< Text is not well-formed UTF-16, or contains U+0000.
    open_failed,             ///< A file could not be opened or created.
    not_a_regular_file,      ///< A path names a directory, a device or anything else that is not a regular file.
    not_seekable,            ///< A stream cannot change its position, which a source needs.
    read_failed,             ///< Reading from a source failed.
    end_of_data,             ///< A read extends beyond the end of a source.
    write_failed,            ///< Writing to a sink, or committing what was written, failed.
    not_an_xisf_unit,        ///< A source starts neither with the signature of a monolithic file nor with XML.
    invalid_header_length,   ///< The header length of a monolithic file is wrong, or counts bytes after the XML.
    reserved_field_not_zero, ///< A field reserved by the specification is not zero.
    header_too_large,        ///< The header is larger than limits::max_header_size, or than a monolithic file can hold.
    invalid_xml,             ///< The header is not well-formed XML 1.0 in UTF-8.
    doctype_not_allowed,     ///< The header has a document type declaration.
    xml_too_deep,            ///< XML elements are nested deeper than limits::max_xml_depth.
    too_many_xml_elements,   ///< The header has more XML elements than limits::max_xml_elements.
    invalid_xml_declaration, ///< The header does not start with the XML declaration of spec §9.5.
    invalid_root_element,    ///< The root element is not an XISF root element (spec §9.5).
    unsupported_version,     ///< The unit is of an XISF version other than 1.0.
    unknown_element,         ///< An XML element that the specification does not define where it appears.
    invalid_uid,             ///< A uid attribute that is not a unique element identifier (spec §11).
    duplicate_uid,           ///< Several core elements have the same uid.
    dangling_reference,      ///< A Reference element names no core element (spec §11.13).
    chained_reference,       ///< A Reference element names another Reference element.
    signature_not_verified,  ///< The unit has an XML signature, which OpenXISF does not verify.
    invalid_location,        ///< A data block location that is not of a form of spec §10.3, or not allowed where it is.
    unsupported_location,    ///< A data block at a location that OpenXISF does not read, such as an external block.
    block_out_of_bounds,     ///< An attached data block extends beyond the end of its file, or into its header.
    invalid_byte_order,      ///< A byteOrder attribute that is neither big nor little (spec §10.4).
    invalid_checksum,        ///< A checksum attribute that is not of the form of spec §10.5.
    unsupported_checksum,    ///< A checksum computed with a hashing algorithm that OpenXISF does not implement.
    checksum_mismatch,       ///< The digest of a data block differs from its checksum attribute.
    invalid_compression,     ///< A compression attribute that is not of the form of spec §10.6.
    unsupported_compression, ///< A data block compressed with a codec that OpenXISF does not implement.
    invalid_subblocks,       ///< A subblocks attribute that is malformed, or disagrees with the sizes of its block.
    allocation_too_large,    ///< Data that need a single allocation larger than limits::max_allocation.
    corrupt_compressed_data, ///< Compressed data that cannot be decoded, or that do not decode to the declared size.
    zstd_window_too_large,   ///< A Zstandard frame needs a window larger than limits::max_zstd_window.
    codec_failure,           ///< A compression library failed for a reason other than the data, such as its version.
    invalid_property,    ///< A Property element lacks an attribute, or serializes its value in a form its type forbids.
    invalid_property_id, ///< A property identifier that is missing, or not of the form of spec §8.4.1.
    duplicate_property_id,     ///< An object has several properties with the same identifier (spec §8.4.1).
    unsupported_property_type, ///< A property type that the specification does not define.
    invalid_property_length,   ///< The length, rows or columns of a property disagree with the size of its data block.
    invalid_complex,           ///< Text is not a complex number in the form of spec §11.1.5.
    invalid_time_point,        ///< Text is not a TimePoint value: an ISO 8601 date and time (spec §8.4.4.4).
    invalid_format_specifier,  ///< Text is not a property format specifier (spec §8.4.3).
    reserved_property_type,    ///< A property with a reserved identifier has another type than the specification's.
    invalid_metadata,         ///< The Metadata element is missing, repeated or incomplete, or holds a foreign property.
    ancillary_data_too_large, ///< The data loaded when a unit is opened exceed limits::max_ancillary_data.
    invalid_image,    ///< An Image element lacks an attribute or its data, or has an attribute not of spec §11.5.
    invalid_geometry, ///< A geometry attribute that is malformed, or describes no pixels or too many to count.
    unsupported_sample_format, ///< A sample format that the specification does not define.
    unsupported_color_space,   ///< A colour space that the specification does not define.
    pixel_data_size_mismatch,  ///< The data of an image are not the size that its geometry and sample format give.
    invalid_image_id,          ///< An image id that is not of the form of spec §11.5.2.
    duplicate_image_id,        ///< Several images of a unit have the same id.
    invalid_reference,         ///< A Reference element names an element that cannot be associated where it is.
    invalid_fits_keyword, ///< A FITSKeyword element lacks a name, or does not follow the rules of FITS (spec §11.6).
    invalid_icc_profile,  ///< An ICCProfile element without a data block (spec §11.7).
    invalid_rgb_working_space, ///< An RGBWorkingSpace element that defines no valid RGB working space (spec §11.8).
    invalid_display_function,  ///< A DisplayFunction element whose parameters are missing or out of range (spec §11.9).
    invalid_color_filter_array, ///< A ColorFilterArray element that does not describe a CFA (spec §11.10).
    invalid_resolution,         ///< A Resolution element whose values are missing or not positive (spec §11.11).
    invalid_thumbnail,          ///< A Thumbnail element that breaks the restrictions of spec §11.12.
    invalid_table,     ///< A Table, Structure, Field, Row or Cell element that breaks the rules of spec §11.2 or §11.3.
    duplicate_element, ///< An image has a second element of a kind it can have once, such as a second ICC profile.
    invalid_character, ///< Text holds a character that an XML header cannot hold, such as a control character.
};

/// Where in a unit a problem was found. Empty or absent members are unknown or do not apply.
struct error_context
{
    std::string element{};                 ///< Path of the XML element.
    std::string attribute{};               ///< Name of the XML attribute.
    std::optional<std::uint64_t> offset{}; ///< Byte offset in the source.

    friend bool operator==(const error_context&, const error_context&) = default;
};

#if defined(_MSC_VER)
// The exceptions derive from std::runtime_error and hold standard-library members, which cl reports for an exported
// class. A C++ interface requires the same standard library on both sides of the DLL boundary anyway.
#pragma warning(push)
#pragma warning(disable : 4251 4275)
#endif

/// Base class of every exception that OpenXISF throws. Copying an error never throws.
///
/// what() returns the message followed by the context, if there is one.
class OPENXISF_API error : public std::runtime_error
{
public:
    /// The message describes the failure without its context, which what() appends.
    error(errc code, std::string_view message, error_context context = {});
    ~error() override;

    /// The reason of the failure.
    [[nodiscard]] errc code() const noexcept;
    /// Where the failure happened.
    [[nodiscard]] const error_context& context() const noexcept;

private:
    errc code_;
    std::shared_ptr<const error_context> context_;
};

/// An operating-system call, a source or a sink failed.
class OPENXISF_API io_error : public error
{
public:
    io_error(errc code, std::string_view message, std::error_code system_code = {}, error_context context = {});
    ~io_error() override;

    /// The operating-system error, when there is one.
    [[nodiscard]] std::error_code system_code() const noexcept;

private:
    std::error_code system_code_;
};

/// The input violates the XISF specification.
class OPENXISF_API invalid_data_error : public error
{
public:
    using error::error;
    ~invalid_data_error() override;
};

/// Data failed an integrity check: a checksum mismatch, a wrong decompressed size or corrupt compressed data.
class OPENXISF_API integrity_error : public error
{
public:
    using error::error;
    ~integrity_error() override;
};

/// Valid input that this build cannot handle, such as an unknown compression codec.
class OPENXISF_API unsupported_error : public error
{
public:
    using error::error;
    ~unsupported_error() override;
};

/// A safety limit was exceeded (see openxisf::limits).
class OPENXISF_API limit_error : public error
{
public:
    using error::error;
    ~limit_error() override;
};

/// A model given to the writer violates the XISF specification. Its context names the element that the offending
/// object would be written as.
class OPENXISF_API validation_error : public error
{
public:
    using error::error;
    ~validation_error() override;
};

/// A precondition of the API was violated, such as an index out of range or a path that is not valid UTF-8.
class OPENXISF_API usage_error : public error
{
public:
    using error::error;
    ~usage_error() override;
};

/// A progress callback asked to stop. The operation left no partial output.
class OPENXISF_API cancelled_error : public error
{
public:
    using error::error;
    ~cancelled_error() override;
};

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

/// How serious a diagnostic is.
enum class severity
{
    info,    ///< Something the specification allows was ignored, such as an extension element.
    warning, ///< A harmless violation of the specification was tolerated, and the data are read as intended.
    error,   ///< An object of the unit is unavailable. Strict reading turns this into a failure.
};

/// A problem found while reading a unit that did not stop the reading (spec §7).
struct diagnostic
{
    openxisf::severity severity;
    errc code;
    std::string message;
    error_context context;
};

} // namespace openxisf
