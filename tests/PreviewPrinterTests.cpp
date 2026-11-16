/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "PreviewPrinter.h"
#include "emubase/Emubase.h"
#include <gtest/gtest.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>

void DebugLog(const wchar_t*) {}

namespace
{
class PreviewPrinterTest : public testing::Test
{
protected:
    std::filesystem::path directory, path;
    std::unique_ptr<CMotherboard> board;
    void SetUp() override
    {
        directory = std::filesystem::temp_directory_path() /
            ("ukncbtl-preview-printer-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        ASSERT_TRUE(std::filesystem::create_directory(directory));
        path = directory / std::filesystem::path(u8"\u65e5\u672c.log");
        CProcessor::Init();
        board = std::make_unique<CMotherboard>();
        board->Reset();
        board->GetCPU()->SetDCLOPin(true);
        board->GetPPU()->SetDCLOPin(true);
    }
    void TearDown() override
    {
        board.reset(); CProcessor::Done();
        std::error_code error;
        std::filesystem::remove(path, error);
        std::filesystem::remove(directory, error);
    }
};

TEST_F(PreviewPrinterTest, WritesPortBytesAndStopsAtSessionEnd)
{
    auto* memory = board->GetPPUMemoryController();
    {
        preview_printer::Session session(*board);
        ASSERT_TRUE(session.Start(path));
        EXPECT_FALSE(session.Start(path));
        EXPECT_TRUE(session.Conflicts(path));
        EXPECT_TRUE(session.Conflicts(directory / "." / path.filename()));
        EXPECT_NE(0, memory->GetWord(0177101, false, false) & 2);
        ASSERT_TRUE(board->SystemFrame());
        memory->SetWord(0177100, false, 0x42);
        memory->SetWord(0177102, false, 0);
        ASSERT_TRUE(board->SystemFrame());
        ASSERT_TRUE(session.Flush());
        std::ifstream file(path, std::ios::binary);
        EXPECT_EQ(std::string(std::istreambuf_iterator<char>(file), {}), "B");
    }
    EXPECT_EQ(0, memory->GetWord(0177101, false, false) & 2);
}

TEST_F(PreviewPrinterTest, FailedOpenCanBeRetriedAndStopIsIdempotent)
{
    preview_printer::Session session(*board);
    EXPECT_FALSE(session.Start(directory / "missing" / "file"));
    EXPECT_FALSE(session.Active());
    ASSERT_TRUE(session.Start(path));
    EXPECT_TRUE(session.Stop());
    EXPECT_TRUE(session.Stop());
    EXPECT_FALSE(session.Conflicts(path));
}

TEST_F(PreviewPrinterTest, CovoxSuppressesOutputUntilDisabled)
{
    preview_printer::Session session(*board);
    ASSERT_TRUE(session.Start(path));
    auto* memory = board->GetPPUMemoryController();
    ASSERT_TRUE(board->SystemFrame());
    board->SetSoundCovox(true);
    memory->SetWord(0177100, false, 0x5a);
    memory->SetWord(0177102, false, 0);
    ASSERT_TRUE(board->SystemFrame());
    ASSERT_TRUE(session.Flush());
    EXPECT_EQ(std::filesystem::file_size(path), 0);
    board->SetSoundCovox(false);
    ASSERT_TRUE(board->SystemFrame());
    ASSERT_TRUE(session.Stop());
    EXPECT_EQ(std::filesystem::file_size(path), 1);
}

TEST_F(PreviewPrinterTest, SynchronizeRestoresHostOnlineState)
{
    preview_printer::Session session(*board);
    auto* memory = board->GetPPUMemoryController();
    ASSERT_TRUE(session.Start(path));
    board->SetParallelOutCallback(nullptr);
    EXPECT_EQ(0, memory->GetWord(0177101, false, false) & 2);
    session.Synchronize();
    EXPECT_NE(0, memory->GetWord(0177101, false, false) & 2);
    ASSERT_TRUE(session.Stop());
    session.Synchronize();
    EXPECT_EQ(0, memory->GetWord(0177101, false, false) & 2);
}
}
