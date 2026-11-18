/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once

#include <SDL3/SDL_scancode.h>
#include <array>
#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

namespace sdl_keyboard
{
struct Event { std::uint8_t scan; bool pressed; };
enum class Source { physical, virtualKey, automatic };

// Coalesce independent host sources that hold the same emulated key.
class Merge
{
public:
    [[nodiscard]] std::optional<Event> Transition(Source source, Event event);
    void Reset();
private:
    std::array<bool, 128> physical_{}, virtual_{}, automatic_{};
};

// Map physical PC keys using the emulator's current LAT/RUS keyboard mode.
[[nodiscard]] std::uint8_t Map(SDL_Scancode key, bool latin);

class Keyboard
{
public:
    // Releases are honored even while captured; key repeats are ignored.
    [[nodiscard]] std::optional<Event> Transition(SDL_Scancode key, bool down, bool repeat, bool captured, bool latin);
    // Release each active emulated key once, including keys shared by physical keys.
    [[nodiscard]] std::vector<Event> ReleaseAll();
private:
    std::array<std::uint8_t, SDL_SCANCODE_COUNT> active_{};
    std::array<std::uint16_t, 128> counts_{};
};

// Deliver queued transitions one per emulated frame so short presses are seen.
class Delivery
{
public:
    void Push(Event event);
    [[nodiscard]] std::optional<Event> Next();
    // Discard pending transitions and release keys already sent to the machine.
    [[nodiscard]] std::vector<Event> Reset();
private:
    std::deque<Event> pending_;
    std::array<bool, 128> delivered_{};
};
}
