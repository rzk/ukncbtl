/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "RomFile.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <vector>

namespace
{
class RomFileTest : public testing::Test
{
protected:
    std::filesystem::path directory, path;
    void SetUp() override
    {
        const auto suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        directory = std::filesystem::temp_directory_path() / ("ukncbtl-rom-" + suffix);
        ASSERT_TRUE(std::filesystem::create_directory(directory));
        path = directory / std::filesystem::path(u8"\u65e5\u672c.rom");
    }
    void TearDown() override
    {
        std::error_code error;
        std::filesystem::remove(path, error);
        std::filesystem::remove(directory, error);
    }
    void Write(std::span<const std::uint8_t> bytes)
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(file);
        if (!bytes.empty()) file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        ASSERT_TRUE(file);
    }
};

TEST_F(RomFileTest, ReadsSystemAndCartridgePrefixesFromUnicodePath)
{
    std::vector<std::uint8_t> source(32768);
    for (std::size_t i = 0; i < source.size(); ++i) source[i] = static_cast<std::uint8_t>(i * 7);
    Write(source);
    std::array<std::uint8_t, 32768> system{};
    ASSERT_TRUE(rom_file::ReadPrefix(path, std::span{system}.first(32256)));
    EXPECT_TRUE(std::equal(system.begin(), system.begin() + 32256, source.begin()));
    EXPECT_TRUE(std::all_of(system.begin() + 32256, system.end(), [](auto value) { return value == 0; }));
    std::array<std::uint8_t, 24 * 1024> cartridge{};
    ASSERT_TRUE(rom_file::ReadPrefix(path, cartridge));
    EXPECT_TRUE(std::equal(cartridge.begin(), cartridge.end(), source.begin()));
}

TEST_F(RomFileTest, AcceptsExactSizeAndRejectsShortFileWithoutChangingOutput)
{
    const std::array<std::uint8_t, 4> source{1, 2, 3, 4};
    Write(source);
    std::array<std::uint8_t, 4> output{};
    ASSERT_TRUE(rom_file::ReadPrefix(path, output));
    EXPECT_EQ(output, source);
    std::array<std::uint8_t, 6> guarded{9, 9, 9, 9, 9, 9};
    const auto result = rom_file::ReadPrefix(path, std::span{guarded}.subspan<1, 5>());
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), rom_file::Error::read_failed);
    EXPECT_EQ(guarded, (std::array<std::uint8_t, 6>{9, 9, 9, 9, 9, 9}));
}

TEST_F(RomFileTest, MissingAndEmptyPathsReportErrorsWithoutChangingOutput)
{
    std::array<std::uint8_t, 2> output{0xaa, 0xbb};
    const auto missing = rom_file::ReadPrefix(path, output);
    ASSERT_FALSE(missing);
    EXPECT_EQ(missing.error(), rom_file::Error::open_failed);
    const auto empty = rom_file::ReadPrefix({}, output);
    ASSERT_FALSE(empty);
    EXPECT_EQ(empty.error(), rom_file::Error::invalid_argument);
    EXPECT_EQ(output, (std::array<std::uint8_t, 2>{0xaa, 0xbb}));
}
}
