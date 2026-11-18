/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "util/ImagePixels.h"
#include <gtest/gtest.h>
#include <array>
#include <climits>
#include <cstdint>
#include <span>

TEST(ImagePixels, ConvertsBottomUpRowsToTopDownBgrAndPreservesGuards)
{
    const std::array<std::uint32_t, 4> input{0xa0ff0000, 0xb000ff00, 0xc00000ff, 0xd0ffffff};
    std::array<std::uint8_t, 14> output;
    output.fill(0xaa);
    ASSERT_TRUE(image_pixels::ToTopDownBgr24(input, std::span{output}.subspan<1, 12>(), 2, 2));
    const std::array<std::uint8_t, 14> expected{
        0xaa, 0xff, 0, 0, 0xff, 0xff, 0xff, 0, 0, 0xff, 0, 0xff, 0, 0xaa};
    EXPECT_EQ(output, expected);
}

TEST(ImagePixels, HandlesOddWidthAndOneRowWithoutPadding)
{
    const std::array<std::uint32_t, 3> input{0x00123456, 0x00abcdef, 0x00010203};
    std::array<std::uint8_t, 11> output;
    output.fill(0xcc);
    ASSERT_TRUE(image_pixels::ToTopDownBgr24(input, output, 3, 1));
    EXPECT_EQ(output, (std::array<std::uint8_t, 11>{0x56,0x34,0x12, 0xef,0xcd,0xab, 3,2,1, 0xcc,0xcc}));
}

TEST(ImagePixels, InvalidArgumentsNeverModifyOutput)
{
    const std::array<std::uint32_t, 4> input{};
    std::array<std::uint8_t, 12> output;
    output.fill(0xaa);
    for (const auto [width, height] : {std::pair{0, 2}, std::pair{2, 0}, std::pair{-1, 2},
                                     std::pair{2, -1}, std::pair{3, 2}, std::pair{INT_MAX, INT_MAX}})
    {
        EXPECT_FALSE(image_pixels::ToTopDownBgr24(input, output, width, height));
        EXPECT_EQ(output, (std::array<std::uint8_t, 12>{0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,0xaa}));
    }
    EXPECT_FALSE(image_pixels::ToTopDownBgr24(input, std::span{output}.first<11>(), 2, 2));
    EXPECT_EQ(output, (std::array<std::uint8_t, 12>{0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,0xaa}));
}
