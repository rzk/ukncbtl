/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "ScreenshotImage.h"
#include "VideoDecoder.h"
#include <gtest/gtest.h>
#include <array>
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
