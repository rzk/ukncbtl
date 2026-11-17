/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once

#include "ScreenshotImage.h"
#include "util/BmpFile.h"
#include "util/PngFile.h"
#include "util/ScreenshotFormat.h"
#include "util/StagedFile.h"
#include <expected>
#include <filesystem>
#include <variant>

namespace screenshot_file
{
using Error = std::variant<bmp_file::Error, png_file::Error, staged_file::Error>;

// Add the chosen extension only when the filename has no extension. Existing
// extensions, including a filename of ".bmp", retain their original meaning.
[[nodiscard]] std::filesystem::path WithDefaultExtension(std::filesystem::path path,
    screenshot_format::Format format);

// Select BMP for a .bmp suffix (case-insensitive), otherwise PNG.
// The destination is replaced only after the encoded file is complete.
[[nodiscard]] std::expected<void, Error> Save(const std::filesystem::path& path,
    const screenshot_image::Image& image);
}
