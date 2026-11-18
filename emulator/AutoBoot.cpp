/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "AutoBoot.h"
#include <array>

bool auto_boot::Sequence::Start(int selection)
{
    if (selection < 1 || selection > 7) return false;
    selection_ = selection; frames_ = 0;
    return true;
}
void auto_boot::Sequence::Cancel() { selection_ = 0; frames_ = 0; }
std::optional<auto_boot::Event> auto_boot::Sequence::FrameCompleted()
{
    if (!Active()) return std::nullopt;
    constexpr std::array<std::uint8_t, 8> digit{0, 0030, 0031, 0032, 0013, 0034, 0035, 0016};
    switch (++frames_)
    {
    case 106: return Event{digit[selection_], true};
    case 110: return Event{digit[selection_], false};
    case 116: return Event{0153, true};
    case 120: Cancel(); return Event{0153, false};
    default: return std::nullopt;
    }
}
