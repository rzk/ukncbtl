/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once

#include <cstdint>
#include <span>

namespace video_palette
{
enum class Mode { rgb, grb, gray };
// Colors are 0x00RRGGBB, indexed by brightness and UKNC color code.
[[nodiscard]] std::span<const std::uint32_t, 128> Get(Mode mode);
}
