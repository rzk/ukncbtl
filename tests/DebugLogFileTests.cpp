/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "util/DebugLogFile.h"
#include <gtest/gtest.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace
{
class DebugLogFileTest : public testing::Test
{
protected:
    std::filesystem::path directory, path;
    void SetUp() override
    {
        const auto suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        directory = std::filesystem::temp_directory_path() / ("ukncbtl-trace-" + suffix);
        ASSERT_TRUE(std::filesystem::create_directory(directory));
        path = directory / std::filesystem::path(u8"\u65e5\u672c.log");
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

TEST_F(DebugLogFileTest, LazilyCreatesAndFlushesBinaryMessagesAtUnicodePath)
{
    DebugLogFile log(path);
    EXPECT_FALSE(std::filesystem::exists(path));
    const std::string message{"a\0b\r\n", 5};
    ASSERT_TRUE(log.Append(message));
    ASSERT_TRUE(log.Append(std::string(5000, 'x')));
    EXPECT_EQ(Read(), message + std::string(5000, 'x'));
    log.Close();
}

TEST_F(DebugLogFileTest, ClearTruncatesAndCloseThenOpenStartsNewLog)
{
    DebugLogFile log(path);
    ASSERT_TRUE(log.Append("first"));
    ASSERT_TRUE(log.Open());
    ASSERT_TRUE(log.Append(" second"));
    EXPECT_EQ(Read(), "first second");
    ASSERT_TRUE(log.Clear());
    EXPECT_EQ(Read(), "");
    ASSERT_TRUE(log.Append("after clear"));
    log.Close();
    log.Close();
    ASSERT_TRUE(log.Append("new session"));
    EXPECT_EQ(Read(), "new session");
    log.Close();
}

TEST_F(DebugLogFileTest, FailedOpenCanBeRetried)
{
    const auto missing = directory / "missing" / "trace.log";
    DebugLogFile log(missing);
    EXPECT_FALSE(log.Open());
    EXPECT_FALSE(log.Append("ignored"));
    ASSERT_TRUE(std::filesystem::create_directory(missing.parent_path()));
    EXPECT_TRUE(log.Append("saved"));
    log.Close();
    std::ifstream file(missing, std::ios::binary);
    ASSERT_TRUE(file);
    const std::string actual{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    EXPECT_EQ(actual, "saved");
    file.close();
    EXPECT_TRUE(std::filesystem::remove(missing));
    EXPECT_TRUE(std::filesystem::remove(missing.parent_path()));
}
}
