/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once
#include "emubase/Defines.h"
#include <cstdint>
#include <optional>

namespace debug_step
{
[[nodiscard]] inline std::optional<std::uint16_t> StepOverTarget(std::uint16_t opcode, std::uint16_t pc, int words)
{
    if ((opcode & ~std::uint16_t{077}) == PI_JMP || (opcode & ~std::uint16_t{0377}) == PI_BR ||
        words < 1 || words > 3) return std::nullopt;
    return static_cast<std::uint16_t>(pc + words * 2);
}
}
