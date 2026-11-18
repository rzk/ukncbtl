/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "EmbeddedRom.h"
#include "RomFile.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <array>

TEST(EmbeddedRom, ContainsExactBytesFromRomAsset)
{
    std::array<std::uint8_t, embedded_rom::Size> original{};
    ASSERT_TRUE(rom_file::ReadPrefix("embedded_rom_test.bin", original));
    EXPECT_TRUE(std::ranges::equal(embedded_rom::Bytes(), original));
}
