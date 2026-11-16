/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once
#include <SDL3/SDL.h>
#include <cstdint>
#include <memory>
#include <span>

namespace sdl_sprite
{
// The renderer must outlive this texture, including until ImGui has rendered it.
class Texture
{
public:
    explicit Texture(SDL_Renderer& renderer) : renderer_(renderer) {}
    [[nodiscard]] bool Update(std::span<const std::uint32_t> topDown);
    [[nodiscard]] SDL_Texture* Get() const { return texture_.get(); }
private:
    SDL_Renderer& renderer_;
    std::unique_ptr<SDL_Texture, decltype(&SDL_DestroyTexture)> texture_{nullptr, SDL_DestroyTexture};
};
}
