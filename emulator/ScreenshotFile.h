/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once

#include "ScreenshotImage.h"
#include "util/BmpFile.h"
#include "util/PngFile.h"
#include <expected>
#include <filesystem>
#include <variant>

namespace screenshot_file
{
using Error = std::variant<bmp_file::Error, png_file::Error>;

// Select BMP for a .bmp suffix (case-insensitive), otherwise PNG.
// Existing files may be truncated if an encoder fails while writing.
[[nodiscard]] std::expected<void, Error> Save(const std::filesystem::path& path,
    const screenshot_image::Image& image);
}
