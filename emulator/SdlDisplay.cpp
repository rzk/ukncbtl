/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "SdlDisplay.h"
#include <algorithm>

bool sdl_display::ToggleFullscreen(SDL_Window& window)
{
    return SDL_SetWindowFullscreen(&window, (SDL_GetWindowFlags(&window) & SDL_WINDOW_FULLSCREEN) == 0);
}

SDL_FRect sdl_display::Viewport(float width, float height, float scaleY, float topUi, float bottomUi)
{
    width = std::max(0.0f, width);
    height = std::max(0.0f, height);
    scaleY = std::max(0.0f, scaleY);
    const float top = std::min(height, std::max(0.0f, topUi) * scaleY);
    const float bottom = std::min(height - top, std::max(0.0f, bottomUi) * scaleY);
    const float available = height - top - bottom;
    const float drawWidth = std::min(width, available * 4.0f / 3.0f);
    const float drawHeight = drawWidth * 3.0f / 4.0f;
    return {(width - drawWidth) / 2.0f, top + (available - drawHeight) / 2.0f, drawWidth, drawHeight};
}
