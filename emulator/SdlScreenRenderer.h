/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once

#include <SDL3/SDL.h>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>

namespace sdl_screen
{
enum class Error { invalid_image, sdl_failure };

// The caller owns the SDL renderer and must keep it alive longer than this
// object. Call on the SDL rendering thread. Draw does not clear or present.
class Renderer
{
public:
    explicit Renderer(SDL_Renderer& renderer);
    [[nodiscard]] std::expected<void, Error> UploadTopDown(int width, int height, std::span<const std::uint32_t> pixels);
    [[nodiscard]] std::expected<void, Error> Draw(const SDL_FRect& destination) const;
    void Invalidate();

private:
    SDL_Renderer* renderer_;
    std::unique_ptr<SDL_Texture, decltype(&SDL_DestroyTexture)> texture_{nullptr, SDL_DestroyTexture};
    int width_ = 0;
    int height_ = 0;
};
}
