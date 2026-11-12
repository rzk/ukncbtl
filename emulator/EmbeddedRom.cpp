/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "EmbeddedRom.h"
#include <array>

namespace
{
constexpr std::array<std::uint8_t, embedded_rom::Size> image{
#include "embedded_rom_data.inc"
};
}

std::span<const std::uint8_t, embedded_rom::Size> embedded_rom::Bytes()
{
    return image;
}
