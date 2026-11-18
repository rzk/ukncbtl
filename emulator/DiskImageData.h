/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace disk_image
{
std::size_t Size(int tracks);
// Invalid tracks or short output leave the buffer untouched. Extra bytes are preserved.
bool Build(int tracks, std::span<std::uint8_t> output);
}
