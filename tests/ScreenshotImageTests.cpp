/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "ScreenshotImage.h"
#include "VideoDecoder.h"
#include <gtest/gtest.h>
#include <array>
#include <climits>
#include <cstddef>
#include <vector>

TEST(ScreenshotImage, CopiesScreenInExpectedRowOrder)
{
    std::vector<std::uint32_t> source(video::Width * video::Height);
    source.front() = 0x112233;
    source.back() = 0xabcdef;
    const auto image = screenshot_image::Prepare(source, 0);
    ASSERT_TRUE(image);
    EXPECT_EQ(image->width, video::Width);
    EXPECT_EQ(image->height, video::Height);
    ASSERT_EQ(image->pixels.size(), source.size());
    EXPECT_EQ(image->pixels[video::Width - 1], 0xabcdefu);
    EXPECT_EQ(image->pixels[image->pixels.size() - video::Width], 0x112233u);
}

TEST(ScreenshotImage, SelectsRequestedModeAndDefaultsInvalidModes)
{
    const std::vector<std::uint32_t> source(video::Width * video::Height, 0x123456);
    const auto doubled = screenshot_image::Prepare(source, 3);
    ASSERT_TRUE(doubled);
    EXPECT_EQ(doubled->width, 640);
    EXPECT_EQ(doubled->height, 576);
    EXPECT_EQ(doubled->pixels.size(), 640u * 576);
    const auto fallback = screenshot_image::Prepare(source, -1);
    const auto defaultMode = screenshot_image::Prepare(source, 1);
    ASSERT_TRUE(fallback);
    ASSERT_TRUE(defaultMode);
    EXPECT_EQ(fallback->width, defaultMode->width);
    EXPECT_EQ(fallback->height, defaultMode->height);
    EXPECT_EQ(fallback->pixels, defaultMode->pixels);
}

TEST(ScreenshotImage, RejectsShortInput)
{
    const std::array<std::uint32_t, 1> source{};
    EXPECT_FALSE(screenshot_image::Prepare(source, 0));
    EXPECT_FALSE(screenshot_image::Prepare({}, 3));
}

TEST(ScreenshotImage, CopiesTopDownPixelsAndPreservesExtraOutput)
{
    const screenshot_image::Image image{2, 2, {0x11ff0000, 0x2200ff00, 0x330000ff, 0x44ffffff}};
    std::array<std::uint32_t, 6> output{1, 1, 1, 1, 1, 1};
    ASSERT_TRUE(screenshot_image::CopyTopDown(image, std::as_writable_bytes(std::span{output}.subspan<1, 4>())));
    EXPECT_EQ(output, (std::array<std::uint32_t, 6>{1, 0x330000ff, 0x44ffffff, 0x11ff0000, 0x2200ff00, 1}));
}

TEST(ScreenshotImage, RejectsInvalidTopDownCopiesWithoutWriting)
{
    std::array<std::uint32_t, 4> output{1, 2, 3, 4};
    const auto unchanged = output;
    for (const auto [width, height] : {std::pair{0, 2}, std::pair{-1, 2}, std::pair{3, 2}, std::pair{INT_MAX, INT_MAX}})
    {
        const screenshot_image::Image image{width, height, {1, 2, 3, 4}};
        EXPECT_FALSE(screenshot_image::CopyTopDown(image, std::as_writable_bytes(std::span{output})));
        EXPECT_EQ(output, unchanged);
    }
    const screenshot_image::Image image{2, 2, {1, 2, 3, 4}};
    EXPECT_FALSE(screenshot_image::CopyTopDown(image, std::as_writable_bytes(std::span{output}.first<3>())));
    EXPECT_EQ(output, unchanged);
}

TEST(ScreenshotImage, CopiesPitchedRowsAndLeavesPaddingUntouched)
{
    const screenshot_image::Image image{2, 2, {1, 2, 3, 4}};
    std::array<std::uint32_t, 7> output{9, 9, 9, 9, 9, 9, 9};
    const auto unchanged = output;
    const auto bytes = std::as_writable_bytes(std::span{output});
    EXPECT_FALSE(screenshot_image::CopyTopDown(image, bytes, sizeof(std::uint32_t)));
    EXPECT_FALSE(screenshot_image::CopyTopDown(image, bytes.first<23>(), 16));
    EXPECT_EQ(output, unchanged);
    ASSERT_TRUE(screenshot_image::CopyTopDown(image, bytes, 16));
    EXPECT_EQ(output, (std::array<std::uint32_t, 7>{3, 4, 9, 9, 1, 2, 9}));
}
