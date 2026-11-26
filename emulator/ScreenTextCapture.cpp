/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "ScreenTextCapture.h"
#include "VideoPalette.h"
#include "emubase/Board.h"
#include "emubase/Memory.h"
#include <array>
#include <memory>
#include <vector>

void screen_text::Capture(CMotherboard& board, CaptureInput& input)
{
    video::CaptureMemory(board, input.memory);
    const auto* memory = board.GetPPUMemoryController();
    const auto byte = [&](std::uint16_t address) {
        const auto word = memory->GetWordView(address & 0xfffe, false, false).value;
        return static_cast<std::uint8_t>(word >> ((address & 1) * 8));
    };
    constexpr std::uint16_t fontTable = 014142 + 32 * 2;
    for (std::size_t index = 0; index < 224; ++index)
    {
        const auto address = memory->GetWordView(static_cast<std::uint16_t>(fontTable + index * 2), false, false).value;
        for (std::size_t row = 0; row < 11; ++row)
            input.currentFont[index * 11 + row] = byte(static_cast<std::uint16_t>(address + row));
    }
    for (std::size_t index = 0; index < FontSize; ++index)
        input.standardFont[index] = byte(static_cast<std::uint16_t>(0120170 + index));
}

std::optional<std::string> screen_text::Recognize(const CaptureInput& input)
{
    std::vector<std::uint32_t> pixels(FrameSize);
    if (!video::RenderText(input.memory, pixels, video_palette::Get(video_palette::Mode::gray))) return std::nullopt;
    std::array<std::uint8_t, OutputSize> output{};
    if (!Convert(input.currentFont, input.standardFont, pixels, output)) return std::nullopt;
    return ToUtf8(output);
}

std::optional<std::string> screen_text::Capture(CMotherboard& board)
{
    auto input = std::make_unique<CaptureInput>();
    Capture(board, *input);
    return Recognize(*input);
}
