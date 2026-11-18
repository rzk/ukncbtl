/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include <SDL3/SDL.h>
#include <gtest/gtest.h>
#include <memory>

TEST(Sdl3Smoke, SavesAndLoadsPngWithoutVideoInitialization)
{
    EXPECT_GE(SDL_GetVersion(), SDL_VERSIONNUM(3, 4, 0));
    const std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> surface(
        SDL_CreateSurface(2, 2, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
    ASSERT_TRUE(surface) << SDL_GetError();
    ASSERT_TRUE(SDL_FillSurfaceRect(surface.get(), nullptr, SDL_MapSurfaceRGBA(surface.get(), 12, 34, 56, 255))) << SDL_GetError();
    const std::unique_ptr<SDL_IOStream, decltype(&SDL_CloseIO)> io(SDL_IOFromDynamicMem(), SDL_CloseIO);
    ASSERT_TRUE(io) << SDL_GetError();
    ASSERT_TRUE(SDL_SavePNG_IO(surface.get(), io.get(), false)) << SDL_GetError();
    ASSERT_EQ(SDL_SeekIO(io.get(), 0, SDL_IO_SEEK_SET), 0) << SDL_GetError();
    const std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> decoded(SDL_LoadPNG_IO(io.get(), false), SDL_DestroySurface);
    ASSERT_TRUE(decoded) << SDL_GetError();
    EXPECT_EQ(decoded->w, 2);
    EXPECT_EQ(decoded->h, 2);
    Uint8 r = 0, g = 0, b = 0, a = 0;
    ASSERT_TRUE(SDL_ReadSurfacePixel(decoded.get(), 1, 1, &r, &g, &b, &a)) << SDL_GetError();
    EXPECT_EQ(r, 12);
    EXPECT_EQ(g, 34);
    EXPECT_EQ(b, 56);
    EXPECT_EQ(a, 255);
}
