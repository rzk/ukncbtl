/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "SdlKeyboard.h"

std::optional<sdl_keyboard::Event> sdl_keyboard::Merge::Transition(Source source, Event event)
{
    if (event.scan == 0 || event.scan >= physical_.size()) return std::nullopt;
    auto& state = source == Source::physical ? physical_[event.scan] : virtual_[event.scan];
    const bool before = physical_[event.scan] || virtual_[event.scan];
    state = event.pressed;
    const bool after = physical_[event.scan] || virtual_[event.scan];
    return before == after ? std::nullopt : std::optional{Event{event.scan, after}};
}

void sdl_keyboard::Merge::Reset() { physical_.fill(false); virtual_.fill(false); }

std::uint8_t sdl_keyboard::Map(SDL_Scancode key, bool latin)
{
    constexpr std::array<std::uint8_t, 26> lat{0072,0076,0050,0057,0033,0047,0055,0156,0073,0027,0052,0056,0112,
        0054,0075,0053,0067,0074,0111,0114,0051,0137,0071,0115,0070,0157};
    constexpr std::array<std::uint8_t, 26> rus{0047,0073,0111,0071,0051,0072,0053,0074,0036,0075,0056,0057,0115,
        0114,0037,0157,0027,0052,0070,0033,0055,0112,0050,0110,0054,0067};
    constexpr std::array<std::uint8_t, 10> digits{0030,0031,0032,0013,0034,0035,0016,0017,0177,0176};
    constexpr std::array<std::uint8_t, 9> keypad{0127,0147,0167,0130,0150,0170,0125,0145,0165};
    constexpr std::array<std::uint8_t, 12> functionKeys{0010,0011,0012,0014,0015,0172,0152,0151,0171,0000,0004,0000};
    if (key >= SDL_SCANCODE_A && key <= SDL_SCANCODE_Z) return (latin ? lat : rus)[key - SDL_SCANCODE_A];
    if (key >= SDL_SCANCODE_1 && key <= SDL_SCANCODE_0) return digits[key - SDL_SCANCODE_1];
    if (key >= SDL_SCANCODE_KP_1 && key <= SDL_SCANCODE_KP_9) return keypad[key - SDL_SCANCODE_KP_1];
    if (key >= SDL_SCANCODE_F1 && key <= SDL_SCANCODE_F12)
        return key == SDL_SCANCODE_F12 ? (latin ? 0155 : 0174) : functionKeys[key - SDL_SCANCODE_F1];
    switch (key)
    {
    case SDL_SCANCODE_RETURN: return 0153;
    case SDL_SCANCODE_KP_ENTER: return 0166;
    case SDL_SCANCODE_ESCAPE: return 0006;
    case SDL_SCANCODE_BACKSPACE: return 0132;
    case SDL_SCANCODE_TAB: return 0026;
    case SDL_SCANCODE_SPACE: return 0113;
    case SDL_SCANCODE_CAPSLOCK: return 0107;
    case SDL_SCANCODE_APPLICATION: return 0106;
    case SDL_SCANCODE_LSHIFT: case SDL_SCANCODE_RSHIFT: return 0105;
    case SDL_SCANCODE_LCTRL: case SDL_SCANCODE_RCTRL: return 0046;
    case SDL_SCANCODE_LALT: case SDL_SCANCODE_RALT: return 0066;
    case SDL_SCANCODE_LEFT: return 0116;
    case SDL_SCANCODE_RIGHT: return 0133;
    case SDL_SCANCODE_UP: return 0154;
    case SDL_SCANCODE_DOWN: return 0134;
    case SDL_SCANCODE_INSERT: return 0171;
    case SDL_SCANCODE_DELETE: return 0172;
    case SDL_SCANCODE_HOME: return latin ? 0155 : 0174;
    case SDL_SCANCODE_END: return 0152;
    case SDL_SCANCODE_PAGEUP: return 0004;
    case SDL_SCANCODE_PAGEDOWN: return 0151;
    case SDL_SCANCODE_KP_0: return 0126;
    case SDL_SCANCODE_KP_MULTIPLY: return 0025;
    case SDL_SCANCODE_KP_MINUS: return 0005;
    case SDL_SCANCODE_KP_PERIOD: return 0146;
    case SDL_SCANCODE_KP_DIVIDE: return 0131;
    case SDL_SCANCODE_MINUS: return 0175;
    case SDL_SCANCODE_SLASH: return 0173;
    case SDL_SCANCODE_GRAVE: return 0007;
    case SDL_SCANCODE_EQUALS: return latin ? 0110 : 0117;
    case SDL_SCANCODE_COMMA: return latin ? 0117 : 0076;
    case SDL_SCANCODE_PERIOD: return latin ? 0135 : 0077;
    case SDL_SCANCODE_SEMICOLON: return latin ? 0174 : 0137;
    case SDL_SCANCODE_APOSTROPHE: return latin ? 0077 : 0136;
    case SDL_SCANCODE_LEFTBRACKET: return latin ? 0036 : 0156;
    case SDL_SCANCODE_RIGHTBRACKET: return latin ? 0037 : 0155;
    case SDL_SCANCODE_BACKSLASH: return latin ? 0136 : 0135;
    default: return 0;
    }
}

std::optional<sdl_keyboard::Event> sdl_keyboard::Keyboard::Transition(
    SDL_Scancode key, bool down, bool repeat, bool captured, bool latin)
{
    const int index = static_cast<int>(key);
    if (index <= 0 || index >= SDL_SCANCODE_COUNT) return std::nullopt;
    auto& active = active_[static_cast<std::size_t>(index)];
    if (down)
    {
        if (repeat || captured || active) return std::nullopt;
        active = Map(key, latin);
        if (!active || counts_[active]++ != 0) return std::nullopt;
        return Event{active, true};
    }
    if (!active) return std::nullopt;
    const auto scan = active;
    active = 0;
    if (--counts_[scan] != 0) return std::nullopt;
    return Event{scan, false};
}

std::vector<sdl_keyboard::Event> sdl_keyboard::Keyboard::ReleaseAll()
{
    std::vector<Event> events;
    for (std::size_t scan = 1; scan < counts_.size(); ++scan)
        if (counts_[scan]) events.push_back({static_cast<std::uint8_t>(scan), false});
    active_.fill(0);
    counts_.fill(0);
    return events;
}

void sdl_keyboard::Delivery::Push(Event event)
{
    if (event.scan > 0 && event.scan < delivered_.size()) pending_.push_back(event);
}

std::optional<sdl_keyboard::Event> sdl_keyboard::Delivery::Next()
{
    if (pending_.empty()) return std::nullopt;
    const auto event = pending_.front();
    pending_.pop_front();
    delivered_[event.scan] = event.pressed;
    return event;
}

std::vector<sdl_keyboard::Event> sdl_keyboard::Delivery::Reset()
{
    std::vector<Event> releases;
    for (std::size_t scan = 1; scan < delivered_.size(); ++scan)
        if (delivered_[scan]) releases.push_back({static_cast<std::uint8_t>(scan), false});
    delivered_.fill(false);
    pending_.clear();
    return releases;
}
