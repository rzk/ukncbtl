/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace screenshot_image
{
struct Image
{
    int width;
    int height;
    std::vector<std::uint32_t> pixels;
};

// Scale a decoded screen using the same mode and pixel layout as ScreenScale.
// Invalid mode numbers select ScreenScale's default mode; short input fails.
[[nodiscard]] std::optional<Image> Prepare(std::span<const std::uint32_t> source, int mode);
}
