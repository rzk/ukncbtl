/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "ScreenshotClipboard.h"
#include "util/BmpFile.h"
#include <SDL3/SDL.h>
#include <array>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace
{
constexpr const char* bmpMimeType = "image/bmp";
constexpr const char* pngMimeType = "image/png";

const void* SDLCALL ClipboardData(void* userdata, const char* mimeType, std::size_t* size)
{
    if (!mimeType) return nullptr;
    const auto& images = *static_cast<const screenshot_clipboard::Prepared*>(userdata);
    const std::string* bytes = nullptr;
    if (std::string_view(mimeType) == bmpMimeType) bytes = &images.bmp;
    else if (std::string_view(mimeType) == pngMimeType) bytes = &images.png;
    else return nullptr;
    *size = bytes->size();
    return bytes->data();
}

void SDLCALL CleanupClipboard(void* userdata)
{
    const std::unique_ptr<screenshot_clipboard::Prepared> images(
        static_cast<screenshot_clipboard::Prepared*>(userdata));
}
}

std::expected<screenshot_clipboard::Prepared, screenshot_clipboard::Error> screenshot_clipboard::Encode(
    const screenshot_image::Image& image)
{
    std::ostringstream stream(std::ios::binary);
    if (!bmp_file::Write(stream, image.pixels, image.width, image.height))
        return std::unexpected(Error::invalid_image);
    Prepared images;
    images.bmp = stream.str();
    auto* source = SDL_IOFromConstMem(images.bmp.data(), images.bmp.size());
    if (!source) return std::unexpected(Error::encode_failed);
    const std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> surface(
        SDL_LoadBMP_IO(source, true), SDL_DestroySurface);
    if (!surface) return std::unexpected(Error::encode_failed);
    const std::unique_ptr<SDL_IOStream, decltype(&SDL_CloseIO)> output(SDL_IOFromDynamicMem(), SDL_CloseIO);
    if (!output || !SDL_SavePNG_IO(surface.get(), output.get(), false))
        return std::unexpected(Error::encode_failed);
    const auto size = SDL_GetIOSize(output.get());
    const auto properties = SDL_GetIOProperties(output.get());
    const auto* data = static_cast<const char*>(SDL_GetPointerProperty(properties, SDL_PROP_IOSTREAM_DYNAMIC_MEMORY_POINTER, nullptr));
    if (size <= 0 || !data) return std::unexpected(Error::encode_failed);
    images.png.assign(data, static_cast<std::size_t>(size));
    return images;
}

std::expected<void, screenshot_clipboard::Error> screenshot_clipboard::SetPrepared(Prepared images)
{
    if (!(SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO)) return std::unexpected(Error::video_not_initialized);
    if (images.bmp.empty() || images.png.empty()) return std::unexpected(Error::invalid_image);
    constexpr std::array mimeTypes{bmpMimeType, pngMimeType};
    // SDL 3.4.16 takes ownership after validating these arguments, including
    // on backend failure; cleanup may run synchronously inside this call.
    auto* ownedImages = std::make_unique<Prepared>(std::move(images)).release();
    if (!SDL_SetClipboardData(ClipboardData, CleanupClipboard, ownedImages, mimeTypes.data(), mimeTypes.size()))
        return std::unexpected(Error::clipboard_failed);
    return {};
}

std::expected<void, screenshot_clipboard::Error> screenshot_clipboard::SetImage(const screenshot_image::Image& image)
{
    if (!(SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO)) return std::unexpected(Error::video_not_initialized);
    auto images = Encode(image);
    if (!images) return std::unexpected(images.error());
    return SetPrepared(std::move(*images));
}
