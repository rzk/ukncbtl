/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "SubtitleFile.h"
#include "AsyncSubtitles.h"
#include "util/IniEncoding.h"
#include <gtest/gtest.h>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

namespace
{
class SubtitleFileTest : public testing::Test
{
protected:
    std::filesystem::path directory, path;
    void SetUp() override
    {
        const auto suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        directory = std::filesystem::temp_directory_path() / ("ukncbtl-subtitles-" + suffix);
        ASSERT_TRUE(std::filesystem::create_directory(directory));
        path = directory / std::filesystem::path(u8"\u65e5\u672c.lst");
    }
    void TearDown() override
    {
        std::error_code error;
        std::filesystem::remove(path, error);
        std::filesystem::remove(directory, error);
    }
    void Write(const std::vector<std::uint8_t>& bytes)
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(file);
        if (!bytes.empty()) file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        ASSERT_TRUE(file);
    }
};

TEST_F(SubtitleFileTest, LoadsUtf16WithOrWithoutBomAtUnicodePath)
{
    auto bytes = ini_encoding::Encode(L";\u041f\u0440\u0438\u0432\u0435\u0442\r\n10 .WORD ;\U0001f600\r\n", ini_encoding::Encoding::utf16_le);
    ASSERT_TRUE(bytes);
    for (bool bom : {true, false})
    {
        auto content = *bytes;
        if (!bom) content.erase(content.begin(), content.begin() + 2);
        Write(content);
        auto result = subtitles::LoadFile(path);
        ASSERT_TRUE(result);
        ASSERT_EQ(result->size(), 2u);
        EXPECT_EQ((*result)[0].address, 8);
        EXPECT_EQ((*result)[0].comment, L";\u041f\u0440\u0438\u0432\u0435\u0442");
        EXPECT_EQ((*result)[1].type, SUBTYPE_COMMENT | SUBTYPE_DATA);
        EXPECT_EQ((*result)[1].comment, L";\U0001f600");
    }
}

TEST_F(SubtitleFileTest, PreservesEmptyBomAndFirstNulBehavior)
{
    Write({0xff, 0xfe});
    const auto bomOnly = subtitles::LoadFile(path);
    ASSERT_TRUE(bomOnly);
    EXPECT_TRUE(bomOnly->empty());
    Write({});
    const auto empty = subtitles::LoadFile(path);
    ASSERT_FALSE(empty);
    EXPECT_EQ(empty.error(), subtitles::FileError::parse_failed);
    Write({0xff, 0xfe, 0, 0, 0, 0xd8});
    const auto terminated = subtitles::LoadFile(path);
    ASSERT_TRUE(terminated);
    EXPECT_TRUE(terminated->empty());
}

TEST_F(SubtitleFileTest, RejectsInvalidEncodingAndMalformedAddress)
{
    for (const auto& bytes : std::vector<std::vector<std::uint8_t>>{
             {0x31}, {0xfe, 0xff, 0, 0x31}, {0xff, 0xfe, 0, 0xd8}})
    {
        Write(bytes);
        const auto result = subtitles::LoadFile(path);
        ASSERT_FALSE(result);
        EXPECT_EQ(result.error(), subtitles::FileError::invalid_encoding);
    }
    const auto badAddress = ini_encoding::Encode(L"8 ;bad", ini_encoding::Encoding::utf16_le);
    ASSERT_TRUE(badAddress);
    Write(*badAddress);
    const auto result = subtitles::LoadFile(path);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), subtitles::FileError::parse_failed);
}

TEST_F(SubtitleFileTest, EnforcesSizeLimitAndReportsMissingFile)
{
    const auto missing = subtitles::LoadFile(path);
    ASSERT_FALSE(missing);
    EXPECT_EQ(missing.error(), subtitles::FileError::open_failed);
    std::vector<std::uint8_t> bytes(1024 * 1024, 0);
    for (std::size_t i = 0; i < bytes.size(); i += 2) bytes[i] = ' ';
    Write(bytes);
    EXPECT_TRUE(subtitles::LoadFile(path));
    bytes.push_back(0);
    Write(bytes);
    const auto oversized = subtitles::LoadFile(path);
    ASSERT_FALSE(oversized);
    EXPECT_EQ(oversized.error(), subtitles::FileError::too_large);
}

TEST_F(SubtitleFileTest, AsyncReadOwnsParsedItemsAndReservesPath)
{
    const auto bytes = ini_encoding::Encode(L"10 .WORD ;first\r\n", ini_encoding::Encoding::utf16_le);
    ASSERT_TRUE(bytes);
    Write(*bytes);
    subtitles::AsyncLoad load;
    ASSERT_TRUE(load.Start(path));
    EXPECT_TRUE(load.Pending());
    EXPECT_EQ(load.Path(), path);
    EXPECT_FALSE(load.Start(path));
    auto result = load.Finish();
    ASSERT_TRUE(result);
    EXPECT_FALSE(result->cancelled);
    EXPECT_EQ(result->path, path);
    ASSERT_TRUE(result->items);
    ASSERT_EQ(result->items->size(), 1u);
    EXPECT_EQ((*result->items)[0].comment, L";first");
    Write({0xff, 0xfe});
    EXPECT_EQ((*result->items)[0].comment, L";first");
    EXPECT_FALSE(load.Pending());
    EXPECT_TRUE(load.Path().empty());
}

TEST_F(SubtitleFileTest, AsyncCancellationAndFailureAllowRetry)
{
    Write({0xff, 0xfe});
    subtitles::AsyncLoad load;
    load.Cancel();
    EXPECT_FALSE(load.Cancelled());
    ASSERT_TRUE(load.Start(path));
    load.Cancel();
    EXPECT_TRUE(load.Cancelled());
    EXPECT_TRUE(load.Pending());
    EXPECT_EQ(load.Path(), path);
    auto cancelled = load.Finish();
    ASSERT_TRUE(cancelled);
    EXPECT_TRUE(cancelled->cancelled);
    EXPECT_FALSE(load.Cancelled());
    ASSERT_TRUE(load.Start(directory / "missing"));
    auto failed = load.Finish();
    ASSERT_TRUE(failed);
    EXPECT_FALSE(failed->cancelled);
    ASSERT_FALSE(failed->items);
    EXPECT_EQ(failed->items.error(), subtitles::FileError::open_failed);
    ASSERT_TRUE(load.Start(path));
    auto retry = load.Finish();
    ASSERT_TRUE(retry);
    EXPECT_FALSE(retry->cancelled);
    EXPECT_TRUE(retry->items);
}
}
