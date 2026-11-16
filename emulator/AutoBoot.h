/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once
#include <cstdint>
#include <optional>

namespace auto_boot
{
struct Event { std::uint8_t scan; bool pressed; };
// Advance once per completed machine frame; stopping frames pauses the sequence.
class Sequence
{
public:
    [[nodiscard]] bool Start(int selection);
    void Cancel();
    [[nodiscard]] std::optional<Event> FrameCompleted();
    [[nodiscard]] bool Active() const { return selection_ != 0; }
private:
    int selection_ = 0;
    unsigned frames_ = 0;
};
}
