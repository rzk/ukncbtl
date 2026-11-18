/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once

#include <filesystem>

namespace screenshot_format
{
enum class Format { png, bmp };

// A case-insensitive .bmp suffix selects BMP; all other paths select PNG.
[[nodiscard]] Format FromPath(const std::filesystem::path& path) noexcept;
}
