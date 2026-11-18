/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <iosfwd>
#include <span>

namespace bmp_file
{
enum class Error { invalid_input, open_failed, write_failed, close_failed };

// Pixels are 0x00RRGGBB in bottom-up row order, as supplied by the renderer.
// Writes uncompressed, bottom-up 24-bit BMP with four-byte row alignment.
[[nodiscard]] bool Write(std::ostream& output, std::span<const std::uint32_t> pixels, int width, int height);
// Existing files are truncated; a write failure can leave a partial image.
[[nodiscard]] std::expected<void, Error> Save(const std::filesystem::path& path,
    std::span<const std::uint32_t> pixels, int width, int height);
}
