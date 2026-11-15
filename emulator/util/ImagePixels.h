/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once

#include <cstdint>
#include <span>

namespace image_pixels
{
// Convert bottom-up 0x00RRGGBB pixels to packed, top-down BGR24 rows.
// Invalid dimensions or short spans leave the output untouched; any output
// bytes beyond the image are preserved.
[[nodiscard]] bool ToTopDownBgr24(std::span<const std::uint32_t> input,
    std::span<std::uint8_t> output, int width, int height);
}
