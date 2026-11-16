/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "SdlDisplay.h"

bool sdl_display::ToggleFullscreen(SDL_Window& window)
{
    return SDL_SetWindowFullscreen(&window, (SDL_GetWindowFlags(&window) & SDL_WINDOW_FULLSCREEN) == 0);
}
