/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "ScreenshotClipboard.h"
#include "AsyncClipboard.h"
#include <SDL3/SDL.h>
#include <gtest/gtest.h>
#include <future>
#include <memory>
#include <string_view>
#include <utility>

namespace
{
using Surface = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;

TEST(ScreenshotClipboard, RejectsCallsWithoutVideoInitialization)
{
    ASSERT_EQ(SDL_WasInit(SDL_INIT_VIDEO), 0u);
    const auto result = screenshot_clipboard::SetImage({1, 1, {0x123456}});
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), screenshot_clipboard::Error::video_not_initialized);
}

class ScreenshotClipboardTest : public testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "dummy", SDL_HINT_OVERRIDE));
        ASSERT_TRUE(SDL_Init(SDL_INIT_VIDEO)) << SDL_GetError();
    }
    void TearDown() override { SDL_Quit(); }

    Surface Read(const char* mime = "image/bmp")
    {
        std::size_t size = 0;
        const std::unique_ptr<void, decltype(&SDL_free)> bytes(SDL_GetClipboardData(mime, &size), SDL_free);
        if (!bytes || !size) return Surface(nullptr, SDL_DestroySurface);
        auto* io = SDL_IOFromConstMem(bytes.get(), size);
        if (!io) return Surface(nullptr, SDL_DestroySurface);
        return Surface(std::string_view(mime) == "image/bmp" ? SDL_LoadBMP_IO(io, true) : SDL_LoadPNG_IO(io, true), SDL_DestroySurface);
    }
};

TEST_F(ScreenshotClipboardTest, OffersImageAndRetainsDataAfterSourceIsDestroyed)
{
    {
        const screenshot_image::Image image{2, 2, {0x00ff0000, 0x0000ff00, 0x000000ff, 0x00ffffff}};
        ASSERT_TRUE(screenshot_clipboard::SetImage(image)) << SDL_GetError();
    }
    ASSERT_TRUE(SDL_HasClipboardData("image/bmp"));
    ASSERT_TRUE(SDL_HasClipboardData("image/png"));
    for (const char* mime : {"image/bmp", "image/png"}) {
        const auto surface = Read(mime);
        ASSERT_TRUE(surface) << SDL_GetError();
        EXPECT_EQ(surface->w, 2);
        EXPECT_EQ(surface->h, 2);
        Uint8 r = 0, g = 0, b = 0, a = 0;
        ASSERT_TRUE(SDL_ReadSurfacePixel(surface.get(), 0, 0, &r, &g, &b, &a));
        EXPECT_EQ(r, 0); EXPECT_EQ(g, 0); EXPECT_EQ(b, 255); EXPECT_EQ(a, 255);
        ASSERT_TRUE(SDL_ReadSurfacePixel(surface.get(), 0, 1, &r, &g, &b, &a));
        EXPECT_EQ(r, 255); EXPECT_EQ(g, 0); EXPECT_EQ(b, 0); EXPECT_EQ(a, 255);
    }
}

TEST_F(ScreenshotClipboardTest, InvalidImagePreservesClipboardAndReplacementWorks)
{
    ASSERT_TRUE(screenshot_clipboard::SetImage({1, 1, {0x00112233}}));
    const auto invalid = screenshot_clipboard::SetImage({2, 2, {0}});
    ASSERT_FALSE(invalid);
    EXPECT_EQ(invalid.error(), screenshot_clipboard::Error::invalid_image);
    auto surface = Read();
    ASSERT_TRUE(surface);
    Uint8 r = 0, g = 0, b = 0, a = 0;
    ASSERT_TRUE(SDL_ReadSurfacePixel(surface.get(), 0, 0, &r, &g, &b, &a));
    EXPECT_EQ(r, 0x11); EXPECT_EQ(g, 0x22); EXPECT_EQ(b, 0x33);
    ASSERT_TRUE(screenshot_clipboard::SetImage({1, 1, {0x00abcdef}}));
    surface = Read();
    ASSERT_TRUE(surface);
    ASSERT_TRUE(SDL_ReadSurfacePixel(surface.get(), 0, 0, &r, &g, &b, &a));
    EXPECT_EQ(r, 0xab); EXPECT_EQ(g, 0xcd); EXPECT_EQ(b, 0xef);
    EXPECT_TRUE(SDL_ClearClipboardData());
    EXPECT_FALSE(SDL_HasClipboardData("image/bmp"));
}

TEST_F(ScreenshotClipboardTest, WorkerEncodesAndVideoThreadInstallsBothFormats)
{
    auto future = std::async(std::launch::async, [] {
        return screenshot_clipboard::Encode({2, 2, {0x00ff0000, 0x0000ff00, 0x000000ff, 0x00ffffff}});
    });
    auto encoded = future.get();
    ASSERT_TRUE(encoded);
    ASSERT_TRUE(screenshot_clipboard::SetPrepared(std::move(*encoded))) << SDL_GetError();
    for (const char* mime : {"image/bmp", "image/png"})
    {
        auto surface = Read(mime);
        ASSERT_TRUE(surface) << SDL_GetError();
        Uint8 red = 0, green = 0, blue = 0, alpha = 0;
        ASSERT_TRUE(SDL_ReadSurfacePixel(surface.get(), 0, 0, &red, &green, &blue, &alpha));
        EXPECT_EQ(red, 0); EXPECT_EQ(green, 0); EXPECT_EQ(blue, 255); EXPECT_EQ(alpha, 255);
    }
    auto invalid = std::async(std::launch::async, [] { return screenshot_clipboard::Encode({2, 2, {0}}); }).get();
    ASSERT_FALSE(invalid);
    EXPECT_EQ(invalid.error(), screenshot_clipboard::Error::invalid_image);
    EXPECT_TRUE(SDL_HasClipboardData("image/bmp"));
}

TEST_F(ScreenshotClipboardTest, AsyncCopyOwnsPixelsAndPublishesOnlyOnCollection)
{
    using screenshot_clipboard::AsyncCopy;
    ASSERT_TRUE(SDL_SetClipboardText("previous"));
    screenshot_image::Image image{1, 1, {0x00112233}};
    AsyncCopy copy;
    ASSERT_TRUE(copy.Start(image));
    EXPECT_TRUE(copy.Pending());
    EXPECT_FALSE(copy.Start(image));
    image.pixels[0] = 0;
    EXPECT_FALSE(SDL_HasClipboardData("image/png"));
    EXPECT_EQ(copy.Finish(), AsyncCopy::Outcome::copied);
    EXPECT_FALSE(copy.Pending());
    for (const char* mime : {"image/bmp", "image/png"})
    {
        const auto surface = Read(mime);
        ASSERT_TRUE(surface);
        Uint8 r, g, b;
        ASSERT_TRUE(SDL_ReadSurfacePixel(surface.get(), 0, 0, &r, &g, &b, nullptr));
        EXPECT_EQ(r, 0x11);
        EXPECT_EQ(g, 0x22);
        EXPECT_EQ(b, 0x33);
    }
}

TEST_F(ScreenshotClipboardTest, SupersededAndFailedCopiesPreserveNewerText)
{
    using screenshot_clipboard::AsyncCopy;
    AsyncCopy copy;
    ASSERT_TRUE(copy.Start({1, 1, {0x00ffffff}}));
    ASSERT_TRUE(copy.SetText("newer"));
    EXPECT_EQ(copy.Finish(), AsyncCopy::Outcome::superseded);
    ASSERT_TRUE(copy.StartScaled({}, 3));
    EXPECT_EQ(copy.Finish(), AsyncCopy::Outcome::failed);
    const std::unique_ptr<char, decltype(&SDL_free)> text(SDL_GetClipboardText(), SDL_free);
    ASSERT_TRUE(text);
    EXPECT_STREQ(text.get(), "newer");
    EXPECT_FALSE(SDL_HasClipboardData("image/png"));
    ASSERT_TRUE(copy.Start({1, 1, {0}}));
    EXPECT_EQ(copy.Finish(), AsyncCopy::Outcome::copied);
}
}
