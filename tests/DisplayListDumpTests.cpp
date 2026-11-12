/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "DisplayListDump.h"
#include "emubase/Emubase.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <sstream>
#include <streambuf>
#include <string>

void DebugLog(const wchar_t*) {}

TEST(DisplayListDump, FormatsShortPaletteAndParameterTags)
{
    const video::DisplayTag shortTag{0000270, video::TagKind::Short, 0, 0, 0012345, 0014000};
    EXPECT_EQ(display_list_dump::FormatLine(0, shortTag), "   0  000270  ------ ------ 012345 014000  \n");
    const video::DisplayTag palette{0001000, video::TagKind::Palette, 0012345, 0076543, 0000200, 0001006};
    EXPECT_EQ(display_list_dump::FormatLine(2, palette), "   2  001000  012345 076543 000200 001006  palette; next: 4-word at 001000\n");
    const video::DisplayTag parameters{0001234, video::TagKind::Parameters, 1, 0x20, 2, 0002002};
    EXPECT_EQ(display_list_dump::FormatLine(19, parameters), "  19  001234  000001 000040 000002 002002  scale 4; next: 4-word at 002000\n");
}

namespace
{
class DisplayListDumpTest : public testing::Test
{
protected:
    std::unique_ptr<CMotherboard> board;
    std::filesystem::path directory, path;
    void SetUp() override
    {
        CProcessor::Init();
        board = std::make_unique<CMotherboard>();
        board->SetRAMWord(0, 0000270, 0012345);
        board->SetRAMWord(0, 0000272, 0014000);
        const auto suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        directory = std::filesystem::temp_directory_path() / ("ukncbtl-display-list-" + suffix);
        ASSERT_TRUE(std::filesystem::create_directory(directory));
        path = directory / std::filesystem::path(u8"\u65e5\u672c.txt");
    }
    void TearDown() override
    {
        board.reset();
        CProcessor::Done();
        std::error_code error;
        std::filesystem::remove(path, error);
        std::filesystem::remove(directory, error);
    }
};

TEST_F(DisplayListDumpTest, WritesEveryLineFromTheBoard)
{
    std::ostringstream output;
    ASSERT_TRUE(display_list_dump::Write(output, *board));
    const auto text = output.str();
    EXPECT_TRUE(text.starts_with("line address  tag 1  tag 2  addr   next   \n   0  000270  ------ ------ 012345 014000  \n"));
    EXPECT_EQ(std::ranges::count(text, '\n'), video::TotalLines + 1);
}

TEST_F(DisplayListDumpTest, ReportsStreamFailure)
{
    class FailingBuffer final : public std::streambuf
    {
        std::streamsize xsputn(const char*, std::streamsize) override { return 0; }
    } buffer;
    std::ostream output(&buffer);
    EXPECT_FALSE(display_list_dump::Write(output, *board));
}

TEST_F(DisplayListDumpTest, SavesToUnicodePathAndReportsOpenFailure)
{
    ASSERT_TRUE(display_list_dump::Save(path, *board));
    std::ifstream file(path);
    ASSERT_TRUE(file);
    const std::string actual{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    std::ostringstream expected;
    ASSERT_TRUE(display_list_dump::Write(expected, *board));
    EXPECT_EQ(actual, expected.str());
    file.close();
    const auto failed = display_list_dump::Save(directory / "missing" / "list.txt", *board);
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error(), display_list_dump::FileError::open_failed);
}
}
