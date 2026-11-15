/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "ScreenshotImage.h"
#include "VideoDecoder.h"
#include "util/ScreenScale.h"
#include <cstring>
#include <limits>

std::optional<screenshot_image::Image> screenshot_image::Prepare(std::span<const std::uint32_t> source, int mode)
{
    if (source.size() < static_cast<std::size_t>(video::Width) * video::Height)
        return std::nullopt;
    const auto& dimensions = screen_scale::GetMode(mode);
    Image image{dimensions.width, dimensions.height,
        std::vector<std::uint32_t>(static_cast<std::size_t>(dimensions.width) * dimensions.height)};
    if (!screen_scale::Apply(mode, source, image.pixels)) return std::nullopt;
    return image;
}

bool screenshot_image::CopyTopDown(const Image& image, std::span<std::byte> output)
{
    if (image.width <= 0 || image.height <= 0) return false;
    const auto count = static_cast<std::uint64_t>(image.width) * static_cast<std::uint64_t>(image.height);
    if (count > image.pixels.size() || count > (std::numeric_limits<std::uint32_t>::max)() / 4 ||
        count * 4 > output.size()) return false;

    const auto width = static_cast<std::size_t>(image.width);
    const auto stride = width * sizeof(std::uint32_t);
    for (int row = 0; row < image.height; ++row)
    {
        const auto source = static_cast<std::size_t>(image.height - row - 1) * width;
        std::memcpy(output.data() + static_cast<std::size_t>(row) * stride,
            image.pixels.data() + source, stride);
    }
    return true;
}
