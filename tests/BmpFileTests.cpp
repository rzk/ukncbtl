/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "util/BmpFile.h"
#include "util/LittleEndian.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <climits>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <streambuf>
#include <string>
#include <vector>

namespace
{
const std::array<std::uint32_t, 4> pixels{0x00ff0000, 0x0000ff00, 0x000000ff, 0x00ffffff};

TEST(BmpFile, WritesExpectedHeaderBottomUpPixelsAndPadding)
{
    std::ostringstream output(std::ios::binary);
    ASSERT_TRUE(bmp_file::Write(output, pixels, 2, 2));
    const auto encoded = output.str();
    ASSERT_EQ(encoded.size(), 70u);
    const auto bytes = std::span{reinterpret_cast<const std::uint8_t*>(encoded.data()), encoded.size()};
    EXPECT_EQ(encoded.substr(0, 2), "BM");
    EXPECT_EQ(little_endian::read<std::uint32_t>(bytes.subspan<2, 4>()), 70u);
    EXPECT_EQ(little_endian::read<std::uint32_t>(bytes.subspan<10, 4>()), 54u);
    EXPECT_EQ(little_endian::read<std::uint32_t>(bytes.subspan<14, 4>()), 40u);
    EXPECT_EQ(little_endian::read<std::uint32_t>(bytes.subspan<18, 4>()), 2u);
    EXPECT_EQ(little_endian::read<std::uint32_t>(bytes.subspan<22, 4>()), 2u);
    EXPECT_EQ(little_endian::read<std::uint16_t>(bytes.subspan<26, 2>()), 1u);
    EXPECT_EQ(little_endian::read<std::uint16_t>(bytes.subspan<28, 2>()), 24u);
    EXPECT_EQ(little_endian::read<std::uint32_t>(bytes.subspan<34, 4>()), 16u);
    const std::array<std::uint8_t, 16> expected{0,0,0xff, 0,0xff,0, 0,0, 0xff,0,0, 0xff,0xff,0xff, 0,0};
    EXPECT_TRUE(std::equal(expected.begin(), expected.end(), bytes.begin() + 54));
}

TEST(BmpFile, RejectsInvalidDimensionsWithoutWriting)
{
    for (const auto [width, height] : {std::pair{0, 2}, std::pair{2, 0}, std::pair{-1, 2},
                                    std::pair{3, 2}, std::pair{INT_MAX, INT_MAX}})
    {
        std::ostringstream output;
        output << "existing";
        EXPECT_FALSE(bmp_file::Write(output, pixels, width, height));
        EXPECT_EQ(output.str(), "existing");
    }
}

TEST(BmpFile, ReportsShortStreamWrites)
{
    class ShortBuffer final : public std::streambuf
    {
        std::streamsize xsputn(const char*, std::streamsize size) override { return size - 1; }
    } buffer;
    std::ostream output(&buffer);
    EXPECT_FALSE(bmp_file::Write(output, pixels, 2, 2));
}

class BmpFileTest : public testing::Test
{
protected:
    std::filesystem::path directory, path;
    void SetUp() override
    {
        const auto suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        directory = std::filesystem::temp_directory_path() / ("ukncbtl-bmp-" + suffix);
        ASSERT_TRUE(std::filesystem::create_directory(directory));
        path = directory / std::filesystem::path(u8"\u65e5\u672c.bmp");
    }
    void TearDown() override
    {
        std::error_code error;
        std::filesystem::remove(path, error);
        std::filesystem::remove(directory, error);
    }
    std::string Read()
    {
        std::ifstream file(path, std::ios::binary);
        EXPECT_TRUE(file);
        return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    }
};

TEST_F(BmpFileTest, SavesAtUnicodePathAndInvalidInputPreservesExistingFile)
{
    ASSERT_TRUE(bmp_file::Save(path, pixels, 2, 2));
    std::ostringstream expected(std::ios::binary);
    ASSERT_TRUE(bmp_file::Write(expected, pixels, 2, 2));
    EXPECT_EQ(Read(), expected.str());
    const auto invalid = bmp_file::Save(path, std::span{pixels}.first(3), 2, 2);
    ASSERT_FALSE(invalid);
    EXPECT_EQ(invalid.error(), bmp_file::Error::invalid_input);
    EXPECT_EQ(Read(), expected.str());
    const auto missing = bmp_file::Save(directory / "missing" / "image.bmp", pixels, 2, 2);
    ASSERT_FALSE(missing);
    EXPECT_EQ(missing.error(), bmp_file::Error::open_failed);
}
}
