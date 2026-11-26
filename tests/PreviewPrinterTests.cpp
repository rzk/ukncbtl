/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "PreviewPrinter.h"
#include "PrinterTestHooks.h"
#include "FrameWorker.h"
#include "emubase/Emubase.h"
#include <gtest/gtest.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <thread>

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

TEST_F(PreviewPrinterTest, WorkerFlushAndStopCommitFramePrinterBytes)
{
    preview_printer::Session session(*board);
    FrameWorker worker([&] { return board->SystemFrame(); });
    ASSERT_TRUE(worker.Start([&] { return session.Start(path); }));
    ASSERT_TRUE(worker.Wait());
    ASSERT_TRUE(worker.Start());
    ASSERT_TRUE(worker.Wait());
    auto* memory = board->GetPPUMemoryController();
    memory->SetWord(0177100, false, 0x42);
    memory->SetWord(0177102, false, 0);
    ASSERT_TRUE(worker.Start());
    ASSERT_TRUE(worker.Wait());
    ASSERT_TRUE(worker.Start([&] { return session.Flush(); }));
    ASSERT_TRUE(worker.Wait());
    EXPECT_EQ(std::filesystem::file_size(path), 1u);
    EXPECT_TRUE(session.Active());
    ASSERT_TRUE(worker.Start([&] { return session.Stop(); }));
    ASSERT_TRUE(worker.Wait());
    EXPECT_FALSE(session.Conflicts(path));
    std::ifstream file(path, std::ios::binary);
    EXPECT_EQ(std::string(std::istreambuf_iterator<char>(file), {}), "B");
}

TEST_F(PreviewPrinterTest, FullBatchAndShutdownPreserveByteOrder)
{
    std::string expected;
    {
        preview_printer::Session session(*board);
        ASSERT_TRUE(session.Start(path));
        auto* memory = board->GetPPUMemoryController();
        // Fill both buffers before the UI collects a result.
        for (unsigned i = 0; i < 8197; ++i)
        {
            const auto byte = static_cast<std::uint8_t>(i);
            memory->SetWord(0177102, false, 0x80);
            ASSERT_TRUE(board->SystemFrame());
            memory->SetWord(0177100, false, byte);
            memory->SetWord(0177102, false, 0);
            ASSERT_TRUE(board->SystemFrame());
            expected.push_back(static_cast<char>(byte));
        }
        ASSERT_TRUE(session.Check());
    }
    std::ifstream file(path, std::ios::binary);
    const std::string actual{std::istreambuf_iterator<char>(file), {}};
    EXPECT_EQ(actual, expected);
}

TEST_F(PreviewPrinterTest, WriterFailureDetachesPrinterAndAllowsRetry)
{
    struct ResetFault { ~ResetFault() { preview_printer::testing::FailWorkerWrites(false); } } resetFault;
    preview_printer::Session session(*board);
    auto* memory = board->GetPPUMemoryController();
    const auto print = [&](std::uint8_t byte) {
        memory->SetWord(0177102, false, 0x80);
        if (!board->SystemFrame()) return false;
        memory->SetWord(0177100, false, byte);
        memory->SetWord(0177102, false, 0);
        return board->SystemFrame();
    };
    ASSERT_TRUE(session.Start(path));
    preview_printer::testing::FailWorkerWrites(true);
    ASSERT_TRUE(print(0x41));
    ASSERT_TRUE(session.Check()); // Submit the queued byte.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (session.Check() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::yield();
    ASSERT_FALSE(session.Active());
    EXPECT_EQ(0, memory->GetWord(0177101, false, false) & 2);
    EXPECT_TRUE(session.Check());
    EXPECT_TRUE(session.Stop());
    preview_printer::testing::FailWorkerWrites(false);
    ASSERT_TRUE(session.Start(path));
    ASSERT_TRUE(print(0x42));
    ASSERT_TRUE(session.Stop());
    std::ifstream file(path, std::ios::binary);
    EXPECT_EQ(std::string(std::istreambuf_iterator<char>(file), {}), "B");
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
