/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "ImagePixels.h"
#include <cstddef>
#include <limits>

bool image_pixels::ToTopDownBgr24(std::span<const std::uint32_t> input,
    std::span<std::uint8_t> output, int width, int height)
{
    if (width <= 0 || height <= 0) return false;
    const auto count = static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height);
    if (count > input.size() || count > (std::numeric_limits<std::uint32_t>::max)() / 4 ||
        count * 3 > output.size()) return false;
    const auto rowWidth = static_cast<std::size_t>(width);
    for (int y = 0; y < height; ++y)
    {
        const auto source = static_cast<std::size_t>(height - y - 1) * rowWidth;
        const auto destination = static_cast<std::size_t>(y) * rowWidth * 3;
        for (std::size_t x = 0; x < rowWidth; ++x)
        {
            const auto pixel = input[source + x];
            output[destination + x * 3] = static_cast<std::uint8_t>(pixel);
            output[destination + x * 3 + 1] = static_cast<std::uint8_t>(pixel >> 8);
            output[destination + x * 3 + 2] = static_cast<std::uint8_t>(pixel >> 16);
        }
    }
    return true;
}
