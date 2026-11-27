/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "PngFile.h"
#include "ImagePixels.h"
#include <SDL3/SDL.h>
#include <limits>
#include <memory>
#include <vector>

std::expected<void, png_file::Error> png_file::Save(const std::filesystem::path& path,
    std::span<const std::uint32_t> pixels, int width, int height)
{
    if (path.empty() || width <= 0 || height <= 0 ||
        width > (std::numeric_limits<int>::max)() / 3)
        return std::unexpected(Error::invalid_input);
    const auto count = static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height);
    if (count > pixels.size() || count > (std::numeric_limits<std::uint32_t>::max)() / 4)
        return std::unexpected(Error::invalid_input);

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(count * 3));
    if (!image_pixels::ToTopDownBgr24(pixels, bytes, width, height))
        return std::unexpected(Error::invalid_input);
    const std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> surface(
        SDL_CreateSurfaceFrom(width, height, SDL_PIXELFORMAT_BGR24, bytes.data(), width * 3), SDL_DestroySurface);
    if (!surface) return std::unexpected(Error::surface_failed);

    const auto utf8 = path.u8string();
    std::unique_ptr<SDL_IOStream, decltype(&SDL_CloseIO)> io(
        SDL_IOFromFile(reinterpret_cast<const char*>(utf8.c_str()), "wb"), SDL_CloseIO);
    if (!io) return std::unexpected(Error::open_failed);
    const bool written = SDL_SavePNG_IO(surface.get(), io.get(), false);
    const bool closed = SDL_CloseIO(io.release());
    if (!written) return std::unexpected(Error::write_failed);
    if (!closed) return std::unexpected(Error::close_failed);
    return {};
}
