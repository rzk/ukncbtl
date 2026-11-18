/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once
#include <SDL3/SDL_events.h>
#include <optional>

namespace sdl_shortcuts
{
enum class Action { fullscreen, toggleRun, reset, step, stepOver };
// Only non-repeated key presses without unrelated modifiers are shortcuts.
[[nodiscard]] std::optional<Action> Classify(const SDL_KeyboardEvent& event);
// F8 remains a UKNC key while running; stepping is only available while paused.
[[nodiscard]] bool Allowed(Action action, bool running, bool captured, bool dialogPending);
}
