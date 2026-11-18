/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once
#include <SDL3/SDL_video.h>
#include <SDL3/SDL_rect.h>

namespace sdl_display
{
// SDL may apply the requested fullscreen change asynchronously.
[[nodiscard]] bool ToggleFullscreen(SDL_Window& window);
// UI heights are logical coordinates; the returned 4:3 rectangle is in pixels.
[[nodiscard]] SDL_FRect Viewport(float width, float height, float scaleY,
    float topUi, float bottomUi);
}
