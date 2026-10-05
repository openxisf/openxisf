// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <openxisf/display.h>
#include <openxisf/error.h>

#include "algorithms/pixel_values.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

namespace openxisf {

namespace {

// The median and the normalized median absolute deviation of the samples of a channel (spec §8.5.7, equation [10]).
struct statistics
{
    double median = 0.0;
    double deviation = 0.0;
};

// The parameters of one channel, or of linked channels (equations [11] to [18]).
struct parameters
{
    double shadows = 0.0;
    double highlights = 1.0;
    double midtones = 0.5;
};

// The sample median of values, which it reorders: the middle value, or the mean of the two middle values of an even
// number of them.
double median_of(std::vector<double>& values)
{
    const auto middle = values.begin() + static_cast<std::ptrdiff_t>(values.size() / 2);
    std::ranges::nth_element(values, middle);
    if (values.size() % 2 != 0) {
        return *middle;
    }
    return (*std::max_element(values.begin(), middle) + *middle) / 2.0;
}

// The statistics of the samples of a channel normalized to [0, 1], NaN left out, or nothing when only NaN remains.
// values is a buffer for the samples, reused from one channel to the next.
template <typename T>
std::optional<statistics> channel_statistics(std::span<const std::byte> pixels, const image_info& image,
                                             const bounds& range, std::size_t channel, std::vector<double>& values)
{
    const std::size_t pixel_count = image.geometry.pixel_count();
    const std::size_t channels = image.geometry.channels;
    values.clear();
    for (std::size_t pixel = 0; pixel < pixel_count; ++pixel) {
        const std::size_t index = detail::sample_index(image.pixel_storage, pixel, channel, pixel_count, channels);
        const double value = detail::normalize(detail::load_value<T>(pixels, index), range);
        if (!std::isnan(value)) {
            values.push_back(value);
        }
    }
    if (values.empty()) {
        return std::nullopt;
    }
    const double median = median_of(values);
    for (double& value : values) {
        value = std::abs(value - median);
    }
    // The constant makes the deviation consistent with the standard deviation of a normal distribution.
    return statistics{.median = median, .deviation = 1.4826 * median_of(values)};
}

// The clipping points of a channel for a given a_c (equations [12] and [13]).
parameters clipping_points(const statistics& channel, bool bright, double clipping)
{
    parameters result;
    if (!bright && channel.deviation != 0.0) {
        result.shadows = std::min(1.0, std::max(0.0, channel.median + (clipping * channel.deviation)));
    }
    if (bright && channel.deviation != 0.0) {
        result.highlights = std::min(1.0, std::max(0.0, channel.median - (clipping * channel.deviation)));
    }
    return result;
}

// The midtones balance (equations [14] and [18]).
double midtones_of(double median, const parameters& clipped, bool bright, double target)
{
    return bright ? midtones_transfer(target, clipped.highlights - median)
                  : midtones_transfer(median - clipped.shadows, target);
}

// One set of parameters for the nominal channels (equations [15] to [18]), from those that have samples.
parameters linked_parameters(const std::vector<statistics>& channels, const adaptive_display_options& options)
{
    const auto count = static_cast<double>(channels.size());
    const bool bright = std::ranges::all_of(channels, [](const statistics& channel) { return channel.median > 0.5; });
    parameters result{.shadows = 0.0, .highlights = 0.0, .midtones = 0.5};
    double median = 0.0;
    for (const statistics& channel : channels) {
        const parameters clipped = clipping_points(channel, bright, options.clipping);
        result.shadows += clipped.shadows / count;
        result.highlights += clipped.highlights / count;
        median += channel.median / count;
    }
    result.midtones = midtones_of(median, result, bright, options.target_background);
    return result;
}

void check_options(const adaptive_display_options& options)
{
    const double background = options.target_background;
    if (std::isnan(background) || background < 0.0 || background > 1.0) {
        throw usage_error(errc::invalid_argument, "the target background of an adaptive display function is in [0, 1]");
    }
    if (!std::isfinite(options.clipping) || options.clipping > 0.0) {
        throw usage_error(errc::invalid_argument,
                          "the clipping point of an adaptive display function is finite and not above zero");
    }
}

} // namespace

double midtones_transfer(double x, double midtones) noexcept
{
    // Equation [5]. Its cases overlap for m = 0 and m = 1; as in PixInsight, 0 and 1 keep their values then.
    if (x == 0.0) {
        return 0.0;
    }
    if (x == 1.0) {
        return 1.0;
    }
    if (x == midtones) {
        return 0.5;
    }
    return (midtones - 1.0) * x / ((((2.0 * midtones) - 1.0) * x) - midtones);
}

double apply_display_function(const display_function& function, std::size_t component, double x)
{
    if (component > 3) {
        throw usage_error(errc::invalid_argument,
                          "a display function has components 0 to 3, not " + std::to_string(component));
    }
    const double shadows = function.shadows[component];
    const double highlights = function.highlights[component];
    // Equation [6].
    double clipped = 0.0;
    if (shadows == highlights) {
        clipped = shadows;
    } else if (x < shadows) {
        clipped = 0.0;
    } else if (x > highlights) {
        clipped = 1.0;
    } else {
        clipped = (x - shadows) / (highlights - shadows);
    }
    const double transferred = midtones_transfer(clipped, function.midtones[component]);
    // Equation [7].
    const double low = function.shadows_expansion[component];
    const double high = function.highlights_expansion[component];
    return (transferred - low) / (high - low);
}

display_function adaptive_display_function(std::span<const std::byte> pixels, const image_info& image,
                                           const adaptive_display_options& options)
{
    if (image.color_space == color_space::cie_lab) {
        throw usage_error(
            errc::invalid_argument,
            "adaptive_display_function() needs a Gray or RGB image; convert CIE L*a*b* data to RGB first");
    }
    const std::size_t nominal = nominal_channels(image.color_space);
    if (image.geometry.channels < nominal) {
        throw usage_error(errc::invalid_argument,
                          "adaptive_display_function() needs the nominal channels of the image");
    }
    const bounds range = detail::checked_range(image, pixels.size(), "adaptive_display_function()");
    check_options(options);

    // The statistics of each nominal channel, empty for a channel of NaN alone.
    std::vector<std::optional<statistics>> channels(nominal);
    std::vector<double> values;
    detail::visit_real_samples(image.sample_format, [&]<typename T>(std::type_identity<T>) {
        values.reserve(image.geometry.pixel_count());
        for (std::size_t channel = 0; channel < nominal; ++channel) {
            channels[channel] = channel_statistics<T>(pixels, image, range, channel, values);
        }
    });

    display_function result;
    const auto assign = [&result](std::size_t component, const parameters& channel) {
        result.shadows[component] = channel.shadows;
        result.highlights[component] = channel.highlights;
        result.midtones[component] = channel.midtones;
    };
    if (options.linked && nominal == 3) {
        std::vector<statistics> measured;
        for (const std::optional<statistics>& channel : channels) {
            if (channel) {
                measured.push_back(*channel);
            }
        }
        if (!measured.empty()) {
            const parameters linked = linked_parameters(measured, options);
            for (std::size_t component = 0; component < 3; ++component) {
                assign(component, linked);
            }
        }
        return result;
    }
    for (std::size_t component = 0; component < nominal; ++component) {
        if (const std::optional<statistics>& channel = channels[component]) {
            // Equation [11]: a_c.
            const bool bright = channel->median > 0.5;
            parameters separate = clipping_points(*channel, bright, options.clipping);
            separate.midtones = midtones_of(channel->median, separate, bright, options.target_background);
            assign(component, separate);
        }
    }
    return result;
}

} // namespace openxisf
