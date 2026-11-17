/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "PreviewSnapshot.h"
#include "emubase/Emubase.h"
#include <gtest/gtest.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

void DebugLog(const wchar_t*) {}

namespace
{
class PreviewSnapshotTest : public testing::Test
{
protected:
    std::filesystem::path directory, path, invalid;
    std::unique_ptr<CMotherboard> board;
    void SetUp() override
    {
        CProcessor::Init();
        board = std::make_unique<CMotherboard>();
        board->Reset();
        directory = std::filesystem::temp_directory_path() /
            ("ukncbtl-preview-state-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        ASSERT_TRUE(std::filesystem::create_directory(directory));
        path = directory / std::filesystem::path(u8"\u65e5\u672c.uknc");
        invalid = directory / "invalid.uknc";
    }
    void TearDown() override
    {
        board.reset();
        CProcessor::Done();
        std::error_code error;
        std::filesystem::remove(path, error);
        std::filesystem::remove(invalid, error);
        std::filesystem::remove(directory, error);
    }
};

TEST(PreviewSnapshot, AddsDefaultExtensionOnlyWhenMissing)
{
    EXPECT_EQ(preview_snapshot::WithDefaultExtension("state"), std::filesystem::path("state.uknc"));
    EXPECT_EQ(preview_snapshot::WithDefaultExtension("state.other"), std::filesystem::path("state.other"));
    EXPECT_EQ(preview_snapshot::WithDefaultExtension(".UKNC"), std::filesystem::path(".UKNC"));
    EXPECT_TRUE(preview_snapshot::WithDefaultExtension({}).empty());
    EXPECT_EQ(preview_snapshot::WithDefaultExtension(std::filesystem::path(u8"\u65e5\u672c")),
        std::filesystem::path(u8"\u65e5\u672c.uknc"));
}

TEST_F(PreviewSnapshotTest, RestoresBoardAndUptimeAfterStopping)
{
    preview_snapshot::Session session(*board);
    board->GetCPU()->SetReg(0, 012345);
    for (int i = 0; i < 75; ++i) session.FrameCompleted();
    ASSERT_EQ(session.Uptime(), 1u);
    ASSERT_TRUE(session.Save(path));
    board->GetCPU()->SetReg(0, 0);
    for (int i = 0; i < 38; ++i) session.FrameCompleted();
    ASSERT_EQ(session.Uptime(), 2u);
    int stops = 0;
    ASSERT_TRUE(session.Load(path, [&] { ++stops; EXPECT_EQ(board->GetCPU()->GetReg(0), 0); }));
    EXPECT_EQ(stops, 1);
    EXPECT_EQ(board->GetCPU()->GetReg(0), 012345);
    EXPECT_EQ(session.Uptime(), 1u);
    for (int i = 0; i < 49; ++i) session.FrameCompleted();
    EXPECT_EQ(session.Uptime(), 1u);
    session.FrameCompleted();
    EXPECT_EQ(session.Uptime(), 2u);
    session.ResetTracking();
    EXPECT_EQ(session.Uptime(), 0u);
}

TEST_F(PreviewSnapshotTest, InvalidLoadDoesNotStopOrChangeMachine)
{
    preview_snapshot::Session session(*board);
    board->GetCPU()->SetReg(0, 0777);
    for (int i = 0; i < 50; ++i) session.FrameCompleted();
    { std::ofstream output(invalid, std::ios::binary); output << "bad"; }
    int stops = 0;
    EXPECT_FALSE(session.Load(invalid, [&] { ++stops; }));
    EXPECT_FALSE(session.Load(directory / "missing.uknc", [&] { ++stops; }));
    EXPECT_FALSE(session.Save(directory / "missing" / "state.uknc"));
    EXPECT_EQ(stops, 0);
    EXPECT_EQ(board->GetCPU()->GetReg(0), 0777);
    EXPECT_EQ(session.Uptime(), 1u);
}

TEST_F(PreviewSnapshotTest, ReplacesSavedStateAndPreservesOccupiedDirectoryOnFailure)
{
    preview_snapshot::Session session(*board);
    board->GetCPU()->SetReg(0, 0111);
    ASSERT_TRUE(session.Save(path));
    board->GetCPU()->SetReg(0, 0222);
    ASSERT_TRUE(session.Save(path));
    board->GetCPU()->SetReg(0, 0333);
    ASSERT_TRUE(session.Load(path, [] {}));
    EXPECT_EQ(board->GetCPU()->GetReg(0), 0222);
    const auto occupied = directory / "occupied";
    ASSERT_TRUE(std::filesystem::create_directory(occupied));
    { std::ofstream marker(occupied / "keep"); marker << "safe"; }
    EXPECT_FALSE(session.Save(occupied));
    EXPECT_TRUE(std::filesystem::exists(occupied / "keep"));
    std::error_code error;
    std::filesystem::remove(occupied / "keep", error);
    std::filesystem::remove(occupied, error);
}
}
