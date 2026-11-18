/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace virtual_keyboard
{
struct Key { const char* label; std::uint8_t scan; float width; unsigned row; bool sticky = false; };
struct Event { std::uint8_t scan; bool pressed; };
[[nodiscard]] std::span<const Key> Keys();
enum class Legend { latin, cyrillic, both };
// Returns UTF-8 labels for the UKNC's physical key positions.
[[nodiscard]] std::string Label(const Key& key, Legend legend);

// Shift and Ctrl latch until pressed again. Other keys are held by the pointer.
class State
{
public:
    [[nodiscard]] std::optional<Event> Press(const Key& key);
    [[nodiscard]] std::optional<Event> ReleasePointer();
    void Reset();
    [[nodiscard]] bool Down(std::uint8_t scan) const;
private:
    std::array<bool, 128> sticky_{};
    std::uint8_t pointer_ = 0;
};
}
