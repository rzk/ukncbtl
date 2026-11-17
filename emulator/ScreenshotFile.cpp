/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "ScreenshotFile.h"
#include "util/ScreenshotFormat.h"

std::filesystem::path screenshot_file::WithDefaultExtension(std::filesystem::path path,
    screenshot_format::Format format)
{
    const auto name = path.filename().native();
    const auto isPngName = name.size() == 4 && name[0] == '.' &&
        (name[1] == 'p' || name[1] == 'P') && (name[2] == 'n' || name[2] == 'N') &&
        (name[3] == 'g' || name[3] == 'G');
    if (!path.empty() && path.has_filename() && !path.has_extension() &&
        !isPngName && screenshot_format::FromPath(path) != screenshot_format::Format::bmp)
        path += format == screenshot_format::Format::bmp ? ".bmp" : ".png";
    return path;
}

std::expected<void, screenshot_file::Error> screenshot_file::Save(
    const std::filesystem::path& path, const screenshot_image::Image& image)
{
    auto staged = staged_file::File::Create(path);
    if (!staged) return std::unexpected(Error{staged.error()});
    if (screenshot_format::FromPath(path) == screenshot_format::Format::bmp)
    {
        const auto result = bmp_file::Save(staged->Path(), image.pixels, image.width, image.height);
        if (!result) return std::unexpected(Error{result.error()});
    }
    else
    {
        const auto result = png_file::Save(staged->Path(), image.pixels, image.width, image.height);
        if (!result) return std::unexpected(Error{result.error()});
    }
    if (const auto result = staged->Commit(); !result) return std::unexpected(Error{result.error()});
    return {};
}
