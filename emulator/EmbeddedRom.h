/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace embedded_rom
{
inline constexpr std::size_t Size = 32256;
[[nodiscard]] std::span<const std::uint8_t, Size> Bytes();
}
