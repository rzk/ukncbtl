/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "MemoryDump.h"
#include <gtest/gtest.h>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <streambuf>
#include <string>
#include <vector>

TEST(MemoryDump, RangeMatchesWholeWordRoundingAndRejectsInvalidBounds)
{
    for (const auto& [start, end, first, last] : std::array{
             std::array{0, 1, 0, 0}, std::array{1, 2, 0, 0},
             std::array{1, 3, 0, 1}, std::array{2, 4, 1, 1},
             std::array{65534, 65535, 32767, 32767}, std::array{0, 65535, 0, 32767}})
    {
        const auto range = memory_dump::Range(static_cast<std::uint16_t>(start), static_cast<std::uint16_t>(end));
        ASSERT_TRUE(range);
        EXPECT_EQ(range->first, first);
        EXPECT_EQ(range->last, last);
        EXPECT_EQ(range->Count(), static_cast<std::size_t>(last - first + 1));
    }
    EXPECT_FALSE(memory_dump::Range(0, 0));
    EXPECT_FALSE(memory_dump::Range(100, 99));
}

TEST(MemoryDump, WritesWordsInLittleEndianAcrossBufferBoundaries)
{
    std::vector<std::uint16_t> words(2049, 0x1234);
    words.front() = 0x0000;
    words.back() = 0xabcd;
    std::ostringstream stream;
    ASSERT_TRUE(memory_dump::WriteWords(stream, words));
    const auto bytes = stream.str();
    ASSERT_EQ(bytes.size(), words.size() * 2);
    EXPECT_EQ(bytes.substr(0, 4), std::string("\x00\x00\x34\x12", 4));
    EXPECT_EQ(bytes.substr(bytes.size() - 4), std::string("\x34\x12\xcd\xab", 4));
}

TEST(MemoryDump, ShortStreamWriteIsReported)
{
    class ShortBuffer final : public std::streambuf
    {
        std::streamsize xsputn(const char*, std::streamsize size) override { return size - 1; }
    } buffer;
    std::ostream stream(&buffer);
    const std::array<std::uint16_t, 2> words{1, 2};
    EXPECT_FALSE(memory_dump::WriteWords(stream, words));
}

namespace
{
class MemoryDumpFileTest : public testing::Test
{
protected:
    std::filesystem::path directory, path;
    void SetUp() override
    {
        const auto suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        directory = std::filesystem::temp_directory_path() / ("ukncbtl-memory-dump-" + suffix);
        ASSERT_TRUE(std::filesystem::create_directory(directory));
        path = directory / std::filesystem::path(u8"\u65e5\u672c.bin");
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

TEST_F(MemoryDumpFileTest, SavesAtUnicodePathAndTruncatesExistingFile)
{
    const std::array<std::uint16_t, 3> first{0x1234, 0xabcd, 0xff00};
    ASSERT_TRUE(memory_dump::Save(path, first));
    EXPECT_EQ(Read(), std::string("\x34\x12\xcd\xab\x00\xff", 6));
    const std::array<std::uint16_t, 1> second{0x5678};
    ASSERT_TRUE(memory_dump::Save(path, second));
    EXPECT_EQ(Read(), std::string("\x78\x56", 2));
}

TEST_F(MemoryDumpFileTest, InvalidPathsAndOpenFailuresAreReported)
{
    const std::array<std::uint16_t, 1> words{1};
    const auto empty = memory_dump::Save({}, words);
    ASSERT_FALSE(empty);
    EXPECT_EQ(empty.error(), memory_dump::FileError::invalid_path);
    const auto missing = memory_dump::Save(directory / "missing" / "dump.bin", words);
    ASSERT_FALSE(missing);
    EXPECT_EQ(missing.error(), memory_dump::FileError::open_failed);
    EXPECT_FALSE(std::filesystem::exists(path));
}
}
