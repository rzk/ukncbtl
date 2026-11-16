/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "SdlSprite.h"
#include "SpriteDecoder.h"
#include <gtest/gtest.h>
#include <memory>
#include <vector>

TEST(SdlSprite, UploadsOpaquePixelsAndReusesTexture)
{
    const std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> surface(
        SDL_CreateSurface(sprite::Width, sprite::Height, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
    ASSERT_TRUE(surface);
    const std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> renderer(
        SDL_CreateSoftwareRenderer(surface.get()), SDL_DestroyRenderer);
    ASSERT_TRUE(renderer);
    sdl_sprite::Texture texture(*renderer);
    EXPECT_FALSE(texture.Update({}));
    std::vector<std::uint32_t> pixels(sprite::Width * sprite::Height, 0xff112233);
    pixels[0] = 0xffabcdef;
    pixels[(sprite::Height - 1) * sprite::Width] = 0xff123456;
    ASSERT_TRUE(texture.Update(pixels)) << SDL_GetError();
    auto* initial = texture.Get();
    ASSERT_TRUE(SDL_RenderTexture(renderer.get(), initial, nullptr, nullptr));
    ASSERT_TRUE(SDL_RenderPresent(renderer.get()));
    Uint8 r = 0, g = 0, b = 0, a = 0;
    ASSERT_TRUE(SDL_ReadSurfacePixel(surface.get(), 0, 0, &r, &g, &b, &a));
    EXPECT_EQ(r, 0xab); EXPECT_EQ(g, 0xcd); EXPECT_EQ(b, 0xef); EXPECT_EQ(a, 255);
    ASSERT_TRUE(SDL_ReadSurfacePixel(surface.get(), 0, sprite::Height - 1, &r, &g, &b, &a));
    EXPECT_EQ(r, 0x12); EXPECT_EQ(g, 0x34); EXPECT_EQ(b, 0x56);
    ASSERT_TRUE(texture.Update(pixels));
    EXPECT_EQ(texture.Get(), initial);
}
