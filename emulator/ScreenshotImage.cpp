/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "ScreenshotImage.h"
#include "VideoDecoder.h"
#include "util/ScreenScale.h"

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
