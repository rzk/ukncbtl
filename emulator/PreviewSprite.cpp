/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "PreviewSprite.h"
#include "SpriteDecoder.h"
#include "emubase/Board.h"
#include "emubase/Processor.h"
#include <algorithm>

std::optional<std::vector<std::uint32_t>> preview_sprite::Capture(
    CMotherboard& board, std::uint16_t address, int width, int mode)
{
    if (!sprite::PageBytes(width, mode)) return std::nullopt;
    std::vector<std::uint32_t> pixels(sprite::Width * sprite::Height);
    const auto* cpu = board.GetCPU();
    const auto* memory = cpu->GetMemoryControllerConst();
    if (!sprite::Decode(pixels, address, width, mode, [&](std::uint16_t at) {
        return memory->GetWordView(at, cpu->IsHaltMode(), false).value;
    })) return std::nullopt;
    for (int row = 0; row < sprite::Height / 2; ++row)
        std::swap_ranges(pixels.begin() + row * sprite::Width, pixels.begin() + (row + 1) * sprite::Width,
            pixels.begin() + (sprite::Height - 1 - row) * sprite::Width);
    for (auto& pixel : pixels) pixel |= 0xff000000u;
    return pixels;
}
