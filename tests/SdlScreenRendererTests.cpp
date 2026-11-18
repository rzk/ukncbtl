/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "SdlScreenRenderer.h"
#include "VideoDecoder.h"
#include <gtest/gtest.h>
#include <array>
#include <climits>
#include <memory>
#include <vector>

namespace
{
class SdlScreenRendererTest : public testing::Test
{
protected:
    std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> surface{nullptr, SDL_DestroySurface};
    std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> renderer{nullptr, SDL_DestroyRenderer};
    void SetUp() override
    {
        surface.reset(SDL_CreateSurface(4, 4, SDL_PIXELFORMAT_RGBA32));
        ASSERT_TRUE(surface) << SDL_GetError();
        renderer.reset(SDL_CreateSoftwareRenderer(surface.get()));
        ASSERT_TRUE(renderer) << SDL_GetError();
    }
    void ExpectPixel(int x, int y, Uint8 red, Uint8 green, Uint8 blue)
    {
        Uint8 r = 0, g = 0, b = 0, a = 0;
        ASSERT_TRUE(SDL_ReadSurfacePixel(surface.get(), x, y, &r, &g, &b, &a));
        EXPECT_EQ(r, red); EXPECT_EQ(g, green); EXPECT_EQ(b, blue); EXPECT_EQ(a, 255);
    }
};

TEST_F(SdlScreenRendererTest, DrawsNearestNeighborWithExpectedOrientationAndColors)
{
    sdl_screen::Renderer screen(*renderer);
    // Bottom row red/green, top row blue/white; high bytes are ignored.
    const screenshot_image::Image image{2, 2, {0x11ff0000, 0x2200ff00, 0x330000ff, 0x44ffffff}};
    ASSERT_TRUE(screen.Draw(image, SDL_FRect{0, 0, 4, 4})) << SDL_GetError();
    ASSERT_TRUE(SDL_RenderPresent(renderer.get())) << SDL_GetError();
    ExpectPixel(0, 0, 0, 0, 255);
    ExpectPixel(1, 1, 0, 0, 255);
    ExpectPixel(3, 0, 255, 255, 255);
    ExpectPixel(0, 3, 255, 0, 0);
    ExpectPixel(3, 3, 0, 255, 0);
}

TEST_F(SdlScreenRendererTest, ReusesAndResizesTexturesAndHonorsDestination)
{
    sdl_screen::Renderer screen(*renderer);
    ASSERT_TRUE(SDL_SetRenderDrawColor(renderer.get(), 1, 2, 3, 255));
    ASSERT_TRUE(SDL_RenderClear(renderer.get()));
    ASSERT_TRUE(screen.Draw({1, 1, {0x00112233}}, SDL_FRect{1, 1, 2, 2}));
    ASSERT_TRUE(screen.Draw({1, 1, {0x00abcdef}}, SDL_FRect{1, 1, 2, 2}));
    ASSERT_TRUE(SDL_RenderPresent(renderer.get()));
    ExpectPixel(0, 0, 1, 2, 3);
    ExpectPixel(1, 1, 0xab, 0xcd, 0xef);
    ASSERT_TRUE(screen.Draw({2, 1, {0x00ff0000, 0x0000ff00}}, SDL_FRect{0, 0, 4, 4}));
    ASSERT_TRUE(SDL_RenderPresent(renderer.get()));
    ExpectPixel(0, 0, 255, 0, 0);
    ExpectPixel(3, 0, 0, 255, 0);
}

TEST_F(SdlScreenRendererTest, DrawsPreparedEmulatorFrame)
{
    std::vector<std::uint32_t> source(video::Width * video::Height);
    for (int y = 0; y < video::Height; ++y)
        for (int x = 0; x < video::Width; ++x)
            source[y * video::Width + x] = y < video::Height / 2
                ? (x < video::Width / 2 ? 0x00ff0000 : 0x0000ff00)
                : (x < video::Width / 2 ? 0x000000ff : 0x00ffffff);
    const auto image = screenshot_image::Prepare(source, 0);
    ASSERT_TRUE(image);
    sdl_screen::Renderer screen(*renderer);
    ASSERT_TRUE(screen.Draw(*image, SDL_FRect{0, 0, 4, 4})) << SDL_GetError();
    ASSERT_TRUE(SDL_RenderPresent(renderer.get()));
    ExpectPixel(0, 0, 255, 0, 0);
    ExpectPixel(3, 0, 0, 255, 0);
    ExpectPixel(0, 3, 0, 0, 255);
    ExpectPixel(3, 3, 255, 255, 255);
}

TEST_F(SdlScreenRendererTest, RejectsInvalidImagesBeforeDrawing)
{
    sdl_screen::Renderer screen(*renderer);
    const SDL_FRect destination{0, 0, 4, 4};
    ASSERT_TRUE(SDL_SetRenderDrawColor(renderer.get(), 7, 8, 9, 255));
    ASSERT_TRUE(SDL_RenderClear(renderer.get()));
    for (const auto& image : {screenshot_image::Image{0, 1, {}}, screenshot_image::Image{2, 2, {1}},
                              screenshot_image::Image{INT_MAX, INT_MAX, {}}})
    {
        const auto result = screen.Draw(image, destination);
        ASSERT_FALSE(result);
        EXPECT_EQ(result.error(), sdl_screen::Error::invalid_image);
    }
    ASSERT_TRUE(SDL_RenderPresent(renderer.get()));
    ExpectPixel(0, 0, 7, 8, 9);
    ASSERT_TRUE(screen.Draw({1, 1, {0x00123456}}, destination));
    ASSERT_TRUE(SDL_RenderPresent(renderer.get()));
    ExpectPixel(0, 0, 0x12, 0x34, 0x56);
}
}
