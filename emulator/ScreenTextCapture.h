/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once
#include "ScreenText.h"
#include "VideoDecoder.h"
#include <array>
#include <optional>
#include <string>

class CMotherboard;
namespace screen_text
{
struct CaptureInput
{
    video::MemorySnapshot memory;
    std::array<std::uint8_t, FontSize> currentFont{};
    std::array<std::uint8_t, FontSize> standardFont{};
};

// Capture the text display and machine fonts without consuming device registers.
void Capture(CMotherboard& board, CaptureInput& input);
// Decode the display and match glyphs without accessing the live board.
[[nodiscard]] std::optional<std::string> Recognize(const CaptureInput& input);
[[nodiscard]] std::optional<std::string> Capture(CMotherboard& board);
}
