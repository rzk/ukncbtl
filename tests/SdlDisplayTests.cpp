/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "SdlDisplay.h"
#include "ScreenText.h"
#include <SDL3/SDL.h>
#include <gtest/gtest.h>
#include <array>
#include <memory>

namespace
{
TEST(SdlDisplay, FitsViewportBetweenBarsAtDifferentScales)
{
    const auto screen = sdl_display::Viewport(1280, 960, 2, 30, 20);
    EXPECT_NEAR(screen.x, 66.6667f, 0.001f);
    EXPECT_FLOAT_EQ(screen.y, 60);
    EXPECT_NEAR(screen.w, 1146.6666f, 0.001f);
    EXPECT_FLOAT_EQ(screen.h, 860);
    const auto tiny = sdl_display::Viewport(200, 20, 1, 40, 20);
    EXPECT_FLOAT_EQ(tiny.w, 0);
    EXPECT_FLOAT_EQ(tiny.h, 0);
    EXPECT_GE(tiny.y, 0);
}

class SdlDisplayTest : public testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "dummy", SDL_HINT_OVERRIDE));
        ASSERT_TRUE(SDL_Init(SDL_INIT_VIDEO)) << SDL_GetError();
    }
    void TearDown() override { SDL_Quit(); }
};

TEST_F(SdlDisplayTest, TogglesFullscreenAndRestoresWindow)
{
    const std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
        SDL_CreateWindow("test", 640, 480, SDL_WINDOW_HIDDEN), SDL_DestroyWindow);
    ASSERT_TRUE(window) << SDL_GetError();
    ASSERT_TRUE(sdl_display::ToggleFullscreen(*window)) << SDL_GetError();
    ASSERT_TRUE(SDL_SyncWindow(window.get())) << SDL_GetError();
    EXPECT_NE(SDL_GetWindowFlags(window.get()) & SDL_WINDOW_FULLSCREEN, 0u);
    ASSERT_TRUE(sdl_display::ToggleFullscreen(*window)) << SDL_GetError();
    ASSERT_TRUE(SDL_SyncWindow(window.get())) << SDL_GetError();
    EXPECT_EQ(SDL_GetWindowFlags(window.get()) & SDL_WINDOW_FULLSCREEN, 0u);
}

TEST_F(SdlDisplayTest, ClipboardAcceptsConvertedCyrillicAndSymbols)
{
    const std::array<std::uint8_t, 4> bytes{0xe1, 0xc1, 0x80, 0};
    const auto text = screen_text::ToUtf8(bytes);
    ASSERT_TRUE(SDL_SetClipboardText(text.c_str())) << SDL_GetError();
    const std::unique_ptr<char, decltype(&SDL_free)> copy(SDL_GetClipboardText(), SDL_free);
    ASSERT_TRUE(copy);
    EXPECT_EQ(std::string(copy.get()), "\xd0\x90\xd0\xb0\xe2\x86\x91");
}
}
