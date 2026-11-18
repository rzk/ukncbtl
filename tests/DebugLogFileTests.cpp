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

TEST_F(DebugLogFileTest, EncodesUnicodeMessagesAsUtf8WithoutTruncation)
{
    DebugLogFile log(path);
    ASSERT_TRUE(log.AppendText(L"ASCII \u041f\u0440\u0438\u0432\u0435\u0442 \u65e5\u672c \U0001f600\r\n"));
    ASSERT_TRUE(log.AppendText(std::wstring(5000, L'\u00e9')));
    std::string expected = "ASCII \xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82 \xE6\x97\xA5\xE6\x9C\xAC \xF0\x9F\x98\x80\r\n";
    for (int i = 0; i < 5000; ++i) expected += "\xC3\xA9";
    EXPECT_EQ(Read(), expected);
    log.Close();
}

TEST_F(DebugLogFileTest, InvalidUnicodeDoesNotAppendPartialText)
{
    DebugLogFile log(path);
    ASSERT_TRUE(log.AppendText(L"before"));
    const std::wstring invalid{L'a', static_cast<wchar_t>(0xd800), L'b'};
    EXPECT_FALSE(log.AppendText(invalid));
    EXPECT_EQ(Read(), "before");
    ASSERT_TRUE(log.AppendText(L"after"));
    EXPECT_EQ(Read(), "beforeafter");
    log.Close();
}
}
