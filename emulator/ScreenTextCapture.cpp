/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "ScreenTextCapture.h"
#include "ScreenText.h"
#include "VideoDecoder.h"
#include "VideoPalette.h"
#include "emubase/Board.h"
#include "emubase/Memory.h"
#include <array>
#include <vector>

std::optional<std::string> screen_text::Capture(CMotherboard& board)
{
    std::vector<std::uint32_t> pixels(FrameSize);
    if (!video::RenderText(board, pixels, video_palette::Get(video_palette::Mode::gray))) return std::nullopt;
    std::array<std::uint8_t, FontSize> current{}, standard{};
    auto* memory = board.GetPPUMemoryController();
    const auto byte = [&](std::uint16_t address) {
        const auto word = memory->GetWordView(address & 0xfffe, false, false).value;
        return static_cast<std::uint8_t>(word >> ((address & 1) * 8));
    };
    constexpr std::uint16_t fontTable = 014142 + 32 * 2;
    for (std::size_t index = 0; index < 224; ++index)
    {
        const auto address = memory->GetWordView(static_cast<std::uint16_t>(fontTable + index * 2), false, false).value;
        for (std::size_t row = 0; row < 11; ++row)
            current[index * 11 + row] = byte(static_cast<std::uint16_t>(address + row));
    }
    for (std::size_t index = 0; index < FontSize; ++index)
        standard[index] = byte(static_cast<std::uint16_t>(0120170 + index));
    std::array<std::uint8_t, OutputSize> output{};
    if (!Convert(current, standard, pixels, output)) return std::nullopt;
    return ToUtf8(output);
}
