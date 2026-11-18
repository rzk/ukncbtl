/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "SdlShortcuts.h"
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_scancode.h>

std::optional<sdl_shortcuts::Action> sdl_shortcuts::Classify(const SDL_KeyboardEvent& event)
{
    if (event.type != SDL_EVENT_KEY_DOWN || event.repeat ||
        (event.mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI))) return std::nullopt;
    const bool shift = (event.mod & SDL_KMOD_SHIFT) != 0;
    if (event.scancode == SDL_SCANCODE_F8) return shift ? Action::stepOver : Action::step;
    if (shift) return std::nullopt;
    if (event.scancode == SDL_SCANCODE_F10) return Action::fullscreen;
    if (event.scancode == SDL_SCANCODE_MEDIA_PLAY_PAUSE) return Action::toggleRun;
    if (event.scancode == SDL_SCANCODE_MEDIA_STOP) return Action::reset;
    return std::nullopt;
}

bool sdl_shortcuts::Allowed(Action action, bool running, bool captured, bool dialogPending)
{
    if (captured || dialogPending) return false;
    return !running || (action != Action::step && action != Action::stepOver);
}
