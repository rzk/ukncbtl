/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "PrinterOutput.h"
#include <gtest/gtest.h>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

namespace
{
class PrinterOutputTest : public testing::Test
{
protected:
    std::filesystem::path directory, path;
    void SetUp() override
    {
        const auto suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        directory = std::filesystem::temp_directory_path() / ("ukncbtl-printer-" + suffix);
        ASSERT_TRUE(std::filesystem::create_directory(directory));
        path = directory / std::filesystem::path(u8"\u65e5\u672c.log");
    }
    void TearDown() override
    {
        std::error_code error;
        std::filesystem::remove(path, error);
        std::filesystem::remove(directory, error);
    }
    std::vector<std::uint8_t> Read()
    {
        std::ifstream file(path, std::ios::binary);
        EXPECT_TRUE(file);
        return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    }
};

TEST_F(PrinterOutputTest, WritesBinaryBytesToUnicodePathAndFlushes)
{
    PrinterOutput output;
    ASSERT_TRUE(output.Open(path));
    EXPECT_TRUE(output.IsOpen());
    for (const auto byte : {0x00, 0x0a, 0x0d, 0x80, 0xff}) ASSERT_TRUE(output.Write(static_cast<std::uint8_t>(byte)));
    ASSERT_TRUE(output.Flush());
    EXPECT_EQ(Read(), (std::vector<std::uint8_t>{0x00, 0x0a, 0x0d, 0x80, 0xff}));
    EXPECT_TRUE(output.Close());
    EXPECT_FALSE(output.IsOpen());
}

TEST_F(PrinterOutputTest, ReopeningTruncatesAndOpenCannotReplaceAnActiveStream)
{
    PrinterOutput output;
    ASSERT_TRUE(output.Open(path));
    ASSERT_TRUE(output.Write(42));
    EXPECT_FALSE(output.Open(path));
    ASSERT_TRUE(output.Close());
    ASSERT_TRUE(output.Open(path));
    ASSERT_TRUE(output.Write(17));
    ASSERT_TRUE(output.Close());
    EXPECT_EQ(Read(), (std::vector<std::uint8_t>{17}));
}

TEST_F(PrinterOutputTest, FailedOpenCanBeRetriedAndClosedOperationsAreSafe)
{
    PrinterOutput output;
    EXPECT_FALSE(output.Write(42));
    EXPECT_FALSE(output.Flush());
    EXPECT_TRUE(output.Close());
    EXPECT_FALSE(output.Open({}));
    EXPECT_FALSE(output.Open(directory / "missing" / "printer.log"));
    EXPECT_FALSE(output.IsOpen());
    ASSERT_TRUE(output.Open(path));
    ASSERT_TRUE(output.Write(0xff));
    ASSERT_TRUE(output.Close());
    EXPECT_TRUE(output.Close());
    EXPECT_EQ(Read(), (std::vector<std::uint8_t>{0xff}));
}

TEST_F(PrinterOutputTest, WriteAndFlushFailuresAreReportedOnceAndDoNotPreventRetry)
{
    PrinterOutput output;
    EXPECT_FALSE(output.TakeError());
    EXPECT_FALSE(output.Write(42));
    EXPECT_FALSE(output.Flush());
    EXPECT_TRUE(output.TakeError());
    EXPECT_FALSE(output.TakeError());
    ASSERT_TRUE(output.Open(path));
    ASSERT_TRUE(output.Write(17));
    ASSERT_TRUE(output.Flush());
    ASSERT_TRUE(output.Close());
    EXPECT_FALSE(output.TakeError());
    EXPECT_EQ(Read(), (std::vector<std::uint8_t>{17}));
}
}
