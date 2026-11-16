/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "VideoPalette.h"
#include <array>

namespace
{
constexpr std::array<std::uint32_t, 128> MakeColor(bool swapRedGreen)
{
    std::array<std::uint32_t, 128> colors{};
    for (int brightness = 0; brightness < 8; ++brightness)
        for (int color = 0; color < 16; ++color)
        {
            const int level = (color & 8) ? 255 : 128;
            int red = (color & 4) ? level - ((brightness & 4) ? 32 : 0) : 0;
            int green = (color & 2) ? level - ((brightness & 2) ? 32 : 0) : 0;
            const int blue = (color & 1) ? level - ((brightness & 1) ? 32 : 0) : 0;
            if (swapRedGreen) { const int temp = red; red = green; green = temp; }
            colors[brightness * 16 + color] = static_cast<std::uint32_t>((red << 16) | (green << 8) | blue);
        }
    return colors;
}

constexpr std::array<std::uint32_t, 128> MakeGray()
{
    constexpr std::array<std::uint32_t, 8> levels{0, 36, 72, 108, 144, 180, 216, 255};
    std::array<std::uint32_t, 128> colors{};
    for (std::size_t i = 0; i < colors.size(); ++i) colors[i] = levels[i % levels.size()] * 0x010101;
    return colors;
}

constexpr auto rgb = MakeColor(false);
constexpr auto grb = MakeColor(true);
constexpr auto gray = MakeGray();
}

std::span<const std::uint32_t, 128> video_palette::Get(Mode mode)
{
    switch (mode)
    {
    case Mode::grb: return grb;
    case Mode::gray: return gray;
    default: return rgb;
    }
}
