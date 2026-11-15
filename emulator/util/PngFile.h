/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>

namespace png_file
{
enum class Error { invalid_input, surface_failed, open_failed, write_failed, close_failed };

// Pixels are bottom-up 0x00RRGGBB. Existing files are truncated; a failed
// write can leave a partial image. Paths are passed to SDL as UTF-8.
[[nodiscard]] std::expected<void, Error> Save(const std::filesystem::path& path,
    std::span<const std::uint32_t> pixels, int width, int height);
}
