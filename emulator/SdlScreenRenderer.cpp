/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "SdlScreenRenderer.h"
#include "util/Profiler.h"
#include <cstring>
#include <limits>
#include <utility>

sdl_screen::Renderer::Renderer(SDL_Renderer& renderer) : renderer_(&renderer) {}

std::expected<void, sdl_screen::Error> sdl_screen::Renderer::UploadTopDown(
    int width, int height, std::span<const std::uint32_t> pixels)
{
    UKNC_PROFILE_SCOPE("Screen texture upload");
    if (width <= 0 || height <= 0 || width > (std::numeric_limits<int>::max)() / 4)
        return std::unexpected(Error::invalid_image);
    const auto count = static_cast<std::uint64_t>(width) * height;
    if (count > pixels.size() || count > (std::numeric_limits<std::uint32_t>::max)() / 4)
        return std::unexpected(Error::invalid_image);

    if (!texture_ || width_ != width || height_ != height)
    {
        std::unique_ptr<SDL_Texture, decltype(&SDL_DestroyTexture)> replacement(
            SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING,
                width, height), SDL_DestroyTexture);
        if (!replacement || !SDL_SetTextureBlendMode(replacement.get(), SDL_BLENDMODE_NONE) ||
            !SDL_SetTextureScaleMode(replacement.get(), SDL_SCALEMODE_NEAREST))
            return std::unexpected(Error::sdl_failure);
        texture_ = std::move(replacement);
        width_ = width;
        height_ = height;
    }
    void* destination = nullptr;
    int pitch = 0;
    if (!SDL_LockTexture(texture_.get(), nullptr, &destination, &pitch))
    {
        Invalidate();
        return std::unexpected(Error::sdl_failure);
    }
    const auto rowBytes = static_cast<std::size_t>(width) * sizeof(std::uint32_t);
    const bool valid = destination && pitch > 0 && static_cast<std::size_t>(pitch) >= rowBytes &&
        static_cast<std::uint64_t>(pitch) * height <= (std::numeric_limits<std::size_t>::max)();
    if (valid)
    {
        if (static_cast<std::size_t>(pitch) == rowBytes)
            std::memcpy(destination, pixels.data(), rowBytes * height);
        else
            for (int row = 0; row < height; ++row)
                std::memcpy(static_cast<std::byte*>(destination) + static_cast<std::size_t>(row) * pitch,
                    pixels.data() + static_cast<std::size_t>(row) * width, rowBytes);
    }
    SDL_UnlockTexture(texture_.get());
    if (!valid)
    {
        Invalidate();
        return std::unexpected(Error::sdl_failure);
    }
    return {};
}

std::expected<void, sdl_screen::Error> sdl_screen::Renderer::Draw(const SDL_FRect& destination) const
{
    UKNC_PROFILE_SCOPE("Screen draw");
    if (!texture_) return std::unexpected(Error::invalid_image);
    if (!SDL_RenderTexture(renderer_, texture_.get(), nullptr, &destination))
        return std::unexpected(Error::sdl_failure);
    return {};
}

void sdl_screen::Renderer::Invalidate() { texture_.reset(); }
