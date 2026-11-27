/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "SdlScreenRenderer.h"
#include "VideoDecoder.h"
#include "util/ScreenScale.h"
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
    const std::array<std::uint32_t, 4> pixels{0x330000ff, 0x44ffffff, 0x11ff0000, 0x2200ff00};
    ASSERT_TRUE(screen.UploadTopDown(2, 2, pixels)) << SDL_GetError();
    ASSERT_TRUE(screen.Draw(SDL_FRect{0, 0, 4, 4})) << SDL_GetError();
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
    ASSERT_TRUE(screen.UploadTopDown(1, 1, std::array<std::uint32_t, 1>{0x00112233}));
    ASSERT_TRUE(screen.Draw(SDL_FRect{1, 1, 2, 2}));
    ASSERT_TRUE(screen.UploadTopDown(1, 1, std::array<std::uint32_t, 1>{0x00abcdef}));
    ASSERT_TRUE(screen.Draw(SDL_FRect{1, 1, 2, 2}));
    ASSERT_TRUE(SDL_RenderPresent(renderer.get()));
    ExpectPixel(0, 0, 1, 2, 3);
    ExpectPixel(1, 1, 0xab, 0xcd, 0xef);
    ASSERT_TRUE(screen.UploadTopDown(2, 1, std::array<std::uint32_t, 2>{0x00ff0000, 0x0000ff00}));
    ASSERT_TRUE(screen.Draw(SDL_FRect{0, 0, 4, 4}));
    ASSERT_TRUE(SDL_RenderPresent(renderer.get()));
    ExpectPixel(0, 0, 255, 0, 0);
    ExpectPixel(3, 0, 0, 255, 0);
}

TEST_F(SdlScreenRendererTest, DrawsVisibleSourcePixelsWithoutResampling)
{
    sdl_screen::Renderer screen(*renderer);
    ASSERT_TRUE(SDL_SetRenderDrawColor(renderer.get(), 0, 0, 0, 255));
    ASSERT_TRUE(SDL_RenderClear(renderer.get()));
    const std::array<std::uint32_t, 6> pixels{
        0x00ff0000, 0x0000ff00, 0x000000ff,
        0x000000ff, 0x000000ff, 0x000000ff,
    };
    ASSERT_TRUE(screen.UploadTopDown(3, 2, pixels));
    const SDL_FRect source{0, 0, 2, 1};
    ASSERT_TRUE(screen.Draw(SDL_FRect{1, 1, source.w, source.h}, &source));
    ASSERT_TRUE(SDL_RenderPresent(renderer.get()));
    ExpectPixel(1, 1, 255, 0, 0);
    ExpectPixel(2, 1, 0, 255, 0);
    ExpectPixel(3, 1, 0, 0, 0);
    ExpectPixel(1, 2, 0, 0, 0);
}

TEST_F(SdlScreenRendererTest, DrawsPreparedEmulatorFrame)
{
    std::vector<std::uint32_t> source(video::Width * video::Height);
    for (int y = 0; y < video::Height; ++y)
        for (int x = 0; x < video::Width; ++x)
            source[y * video::Width + x] = y < video::Height / 2
                ? (x < video::Width / 2 ? 0x00ff0000 : 0x0000ff00)
                : (x < video::Width / 2 ? 0x000000ff : 0x00ffffff);
    constexpr int mode = 1;
    const auto& size = screen_scale::GetMode(mode);
    std::vector<std::uint32_t> scaled(size.width * size.height);
    ASSERT_TRUE(screen_scale::Apply(mode, source, scaled, screen_scale::RowOrder::topDown));
    sdl_screen::Renderer screen(*renderer);
    ASSERT_TRUE(screen.UploadTopDown(size.width, size.height, scaled)) << SDL_GetError();
    ASSERT_TRUE(screen.Draw(SDL_FRect{0, 0, 4, 4})) << SDL_GetError();
    ASSERT_TRUE(SDL_RenderPresent(renderer.get()));
    ExpectPixel(0, 0, 255, 0, 0);
    ExpectPixel(3, 0, 0, 255, 0);
    ExpectPixel(0, 3, 0, 0, 255);
    ExpectPixel(3, 3, 255, 255, 255);
}

TEST_F(SdlScreenRendererTest, DrawsUploadedTextureAgainAndRecreatesItAfterInvalidation)
{
    sdl_screen::Renderer screen(*renderer);
    const SDL_FRect destination{0, 0, 4, 4};
    EXPECT_FALSE(screen.Draw(destination));
    ASSERT_TRUE(screen.UploadTopDown(1, 2, std::array<std::uint32_t, 2>{0x0000ff00, 0x00ff0000}));
    ASSERT_TRUE(screen.Draw(destination));
    ASSERT_TRUE(SDL_RenderPresent(renderer.get()));
    ExpectPixel(0, 0, 0, 255, 0);
    ExpectPixel(0, 3, 255, 0, 0);

    ASSERT_TRUE(SDL_SetRenderDrawColor(renderer.get(), 1, 2, 3, 255));
    ASSERT_TRUE(SDL_RenderClear(renderer.get()));
    ASSERT_TRUE(screen.Draw(SDL_FRect{1, 1, 2, 2}));
    ASSERT_TRUE(SDL_RenderPresent(renderer.get()));
    ExpectPixel(0, 0, 1, 2, 3);
    ExpectPixel(1, 1, 0, 255, 0);
    ExpectPixel(1, 2, 255, 0, 0);

    screen.Invalidate();
    EXPECT_FALSE(screen.Draw(destination));
    ASSERT_TRUE(screen.UploadTopDown(1, 2, std::array<std::uint32_t, 2>{0x00112233, 0x00abcdef}));
    ASSERT_TRUE(screen.Draw(destination));
    ASSERT_TRUE(SDL_RenderPresent(renderer.get()));
    ExpectPixel(0, 0, 0x11, 0x22, 0x33);
    ExpectPixel(0, 3, 0xab, 0xcd, 0xef);
}

TEST_F(SdlScreenRendererTest, RejectsInvalidImagesBeforeDrawing)
{
    sdl_screen::Renderer screen(*renderer);
    const SDL_FRect destination{0, 0, 4, 4};
    ASSERT_TRUE(SDL_SetRenderDrawColor(renderer.get(), 7, 8, 9, 255));
    ASSERT_TRUE(SDL_RenderClear(renderer.get()));
    const std::array<std::uint32_t, 1> pixel{0x00123456};
    for (const auto dimensions : {std::array{0, 1}, std::array{2, 2}, std::array{INT_MAX, INT_MAX}})
    {
        const auto result = screen.UploadTopDown(dimensions[0], dimensions[1], pixel);
        ASSERT_FALSE(result);
        EXPECT_EQ(result.error(), sdl_screen::Error::invalid_image);
    }
    ASSERT_TRUE(SDL_RenderPresent(renderer.get()));
    ExpectPixel(0, 0, 7, 8, 9);
    ASSERT_TRUE(screen.UploadTopDown(1, 1, pixel));
    EXPECT_FALSE(screen.UploadTopDown(1, 2, pixel));
    ASSERT_TRUE(screen.Draw(destination));
    ASSERT_TRUE(SDL_RenderPresent(renderer.get()));
    ExpectPixel(0, 0, 0x12, 0x34, 0x56);
}
}
