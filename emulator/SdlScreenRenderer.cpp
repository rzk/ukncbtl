/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "SdlScreenRenderer.h"
#include "util/Profiler.h"
#include <limits>
#include <span>
#include <utility>

sdl_screen::Renderer::Renderer(SDL_Renderer& renderer) : renderer_(&renderer) {}

std::expected<void, sdl_screen::Error> sdl_screen::Renderer::Draw(
    const screenshot_image::Image& image, const SDL_FRect& destination)
{
    UKNC_PROFILE_SCOPE("Screen draw");
    if (image.width <= 0 || image.height <= 0 ||
        image.width > (std::numeric_limits<int>::max)() / 4)
        return std::unexpected(Error::invalid_image);
    const auto count = static_cast<std::uint64_t>(image.width) * static_cast<std::uint64_t>(image.height);
    if (count > image.pixels.size() || count > (std::numeric_limits<std::uint32_t>::max)() / 4)
        return std::unexpected(Error::invalid_image);

    topDown_.resize(static_cast<std::size_t>(count));
    if (!screenshot_image::CopyTopDown(image, std::as_writable_bytes(std::span{topDown_})))
        return std::unexpected(Error::invalid_image);

    if (!texture_ || width_ != image.width || height_ != image.height)
    {
        std::unique_ptr<SDL_Texture, decltype(&SDL_DestroyTexture)> replacement(
            SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING,
                image.width, image.height), SDL_DestroyTexture);
        if (!replacement || !SDL_SetTextureBlendMode(replacement.get(), SDL_BLENDMODE_NONE) ||
            !SDL_SetTextureScaleMode(replacement.get(), SDL_SCALEMODE_NEAREST))
            return std::unexpected(Error::sdl_failure);
        texture_ = std::move(replacement);
        width_ = image.width;
        height_ = image.height;
    }
    if (!SDL_UpdateTexture(texture_.get(), nullptr, topDown_.data(), image.width * 4) ||
        !SDL_RenderTexture(renderer_, texture_.get(), nullptr, &destination))
        return std::unexpected(Error::sdl_failure);
    return {};
}
