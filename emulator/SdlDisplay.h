/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once
#include <SDL3/SDL_video.h>

namespace sdl_display
{
// SDL may apply the requested fullscreen change asynchronously.
[[nodiscard]] bool ToggleFullscreen(SDL_Window& window);
}
