/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "ScreenshotFile.h"
#include "util/ScreenshotFormat.h"

std::expected<void, screenshot_file::Error> screenshot_file::Save(
    const std::filesystem::path& path, const screenshot_image::Image& image)
{
    if (screenshot_format::FromPath(path) == screenshot_format::Format::bmp)
    {
        const auto result = bmp_file::Save(path, image.pixels, image.width, image.height);
        if (!result) return std::unexpected(Error{result.error()});
    }
    else
    {
        const auto result = png_file::Save(path, image.pixels, image.width, image.height);
        if (!result) return std::unexpected(Error{result.error()});
    }
    return {};
}
