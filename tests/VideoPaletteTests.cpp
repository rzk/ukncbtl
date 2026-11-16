/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "VideoPalette.h"
#include <gtest/gtest.h>
#include <cstdint>

namespace
{
std::uint32_t Hash(std::span<const std::uint32_t, 128> palette)
{
    std::uint32_t hash = 2166136261u;
    for (const auto color : palette) hash = (hash ^ color) * 16777619u;
    return hash;
}
}

TEST(VideoPalette, MatchesLegacyPaletteChecksums)
{
    // Hashes are calculated from the original 128-entry Windows tables.
    EXPECT_EQ(Hash(video_palette::Get(video_palette::Mode::rgb)), 0x0068b145u);
    EXPECT_EQ(Hash(video_palette::Get(video_palette::Mode::grb)), 0xec00d945u);
    EXPECT_EQ(Hash(video_palette::Get(video_palette::Mode::gray)), 0x1395d0c5u);
}

TEST(VideoPalette, UsesExpectedChannelOrderAndDefaultsToRgb)
{
    const auto rgb = video_palette::Get(video_palette::Mode::rgb);
    const auto grb = video_palette::Get(video_palette::Mode::grb);
    const auto gray = video_palette::Get(video_palette::Mode::gray);
    EXPECT_EQ(rgb[4], 0x800000u);
    EXPECT_EQ(grb[4], 0x008000u);
    EXPECT_EQ(gray[7], 0xffffffu);
    EXPECT_EQ(gray[15], 0xffffffu);
    EXPECT_EQ(video_palette::Get(static_cast<video_palette::Mode>(99)).data(), rgb.data());
}
