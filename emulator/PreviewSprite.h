/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once
#include <cstdint>
#include <optional>
#include <vector>

class CMotherboard;
namespace preview_sprite
{
// Returns opaque ARGB pixels in top-down order, read through the CPU debugger view.
[[nodiscard]] std::optional<std::vector<std::uint32_t>> Capture(
    CMotherboard& board, std::uint16_t address, int width, int mode);
}
