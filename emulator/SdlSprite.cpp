/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "SdlSprite.h"
#include "SpriteDecoder.h"

bool sdl_sprite::Texture::Update(std::span<const std::uint32_t> pixels)
{
    if (pixels.size() != sprite::Width * sprite::Height) return false;
    if (!texture_)
    {
        texture_.reset(SDL_CreateTexture(&renderer_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
            sprite::Width, sprite::Height));
        if (!texture_ || !SDL_SetTextureBlendMode(texture_.get(), SDL_BLENDMODE_BLEND) ||
            !SDL_SetTextureScaleMode(texture_.get(), SDL_SCALEMODE_NEAREST))
        { texture_.reset(); return false; }
    }
    return SDL_UpdateTexture(texture_.get(), nullptr, pixels.data(), sprite::Width * sizeof(std::uint32_t));
}
