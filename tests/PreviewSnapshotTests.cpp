/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "PreviewSnapshot.h"
#include "AsyncSnapshot.h"
#include "FrameWorker.h"
#include "emubase/Emubase.h"
#include <gtest/gtest.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

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

TEST_F(PreviewSnapshotTest, WorkerCanResumeAfterStateLoadBetweenFrames)
{
    board->GetCPU()->SetDCLOPin(true);
    board->GetPPU()->SetDCLOPin(true);
    board->GetCPU()->SetReg(0, 012345);
    preview_snapshot::Session session(*board);
    ASSERT_TRUE(session.Save(path));
    FrameWorker worker([&] { return board->SystemFrame(); });
    ASSERT_TRUE(worker.Start());
    ASSERT_TRUE(worker.Wait());
    session.FrameCompleted();
    board->GetCPU()->SetReg(0, 0777);
    int stops = 0;
    EXPECT_FALSE(session.Load(invalid, [&] { ++stops; }));
    EXPECT_EQ(stops, 0);
    ASSERT_TRUE(session.Load(path, [&] { ++stops; }));
    EXPECT_EQ(stops, 1);
    EXPECT_EQ(board->GetCPU()->GetReg(0), 012345);
    ASSERT_TRUE(worker.Start());
    ASSERT_TRUE(worker.Wait());
    session.FrameCompleted();
    EXPECT_TRUE(session.Save(path));
}

TEST_F(PreviewSnapshotTest, AsyncSaveOwnsCapturedStateWhileWorkerAdvances)
{
    board->GetCPU()->SetDCLOPin(true);
    board->GetPPU()->SetDCLOPin(true);
    board->GetCPU()->SetReg(0, 012345);
    preview_snapshot::Session session(*board);
    preview_snapshot::AsyncSave save;
    std::vector<std::uint8_t> image;
    FrameWorker worker([&] { return board->SystemFrame(); });
    ASSERT_TRUE(worker.Start([&] {
        image = session.Capture();
        return true;
    }));
    ASSERT_TRUE(worker.Wait());
    ASSERT_TRUE(save.Start(path, std::move(image)));
    EXPECT_TRUE(save.Pending());
    EXPECT_EQ(save.Path(), path);
    EXPECT_FALSE(save.Start(path, session.Capture()));
    board->GetCPU()->SetReg(0, 0777);
    ASSERT_TRUE(worker.Start());
    ASSERT_TRUE(worker.Wait());
    ASSERT_EQ(save.Finish(), std::optional<bool>(true));
    EXPECT_FALSE(save.Pending());
    ASSERT_TRUE(session.Load(path, [] {}));
    EXPECT_EQ(board->GetCPU()->GetReg(0), 012345);
}

TEST_F(PreviewSnapshotTest, AsyncSaveFailurePreservesExistingStateAndAllowsRetry)
{
    preview_snapshot::Session session(*board);
    board->GetCPU()->SetReg(0, 0111);
    ASSERT_TRUE(session.Save(path));
    preview_snapshot::AsyncSave save;
    ASSERT_TRUE(save.Start(path, {}));
    ASSERT_EQ(save.Finish(), std::optional<bool>(false));
    board->GetCPU()->SetReg(0, 0222);
    ASSERT_TRUE(session.Load(path, [] {}));
    EXPECT_EQ(board->GetCPU()->GetReg(0), 0111);
    board->GetCPU()->SetReg(0, 0333);
    ASSERT_TRUE(save.Start(path, session.Capture()));
    ASSERT_EQ(save.Finish(), std::optional<bool>(true));
    ASSERT_TRUE(session.Load(path, [] {}));
    EXPECT_EQ(board->GetCPU()->GetReg(0), 0333);
}

TEST_F(PreviewSnapshotTest, AsyncSaveDestructorCompletesOutstandingWrite)
{
    preview_snapshot::Session session(*board);
    board->GetCPU()->SetReg(0, 012345);
    { preview_snapshot::AsyncSave save; ASSERT_TRUE(save.Start(path, session.Capture())); }
    board->GetCPU()->SetReg(0, 0);
    ASSERT_TRUE(session.Load(path, [] {}));
    EXPECT_EQ(board->GetCPU()->GetReg(0), 012345);
}

TEST_F(PreviewSnapshotTest, AsyncLoadReadsOwnedImageWhileFrameWorkerRuns)
{
    board->GetCPU()->SetDCLOPin(true);
    board->GetPPU()->SetDCLOPin(true);
    board->GetCPU()->SetReg(0, 012345);
    preview_snapshot::Session session(*board);
    ASSERT_TRUE(session.Save(path));
    preview_snapshot::AsyncLoad load;
    ASSERT_TRUE(load.Start(path));
    EXPECT_TRUE(load.Pending());
    EXPECT_EQ(load.Path(), path);
    EXPECT_FALSE(load.Start(path));
    FrameWorker worker([&] { return board->SystemFrame(); });
    ASSERT_TRUE(worker.Start());
    auto loaded = load.Finish();
    ASSERT_TRUE(worker.Wait());
    ASSERT_TRUE(loaded);
    ASSERT_FALSE(loaded->cancelled);
    ASSERT_TRUE(loaded->image);
    EXPECT_FALSE(load.Pending());
    board->GetCPU()->SetReg(0, 0777);
    ASSERT_TRUE(session.Save(path)); // The collected bytes do not depend on the file anymore.
    ASSERT_TRUE(worker.Start([&, image = std::move(*loaded->image)] { return session.Restore(image, [] {}); }));
    ASSERT_TRUE(worker.Wait());
    EXPECT_EQ(board->GetCPU()->GetReg(0), 012345);
    ASSERT_TRUE(worker.Start());
    ASSERT_TRUE(worker.Wait());
}

TEST_F(PreviewSnapshotTest, AsyncLoadRejectsInvalidImageAndCanRetry)
{
    preview_snapshot::Session session(*board);
    board->GetCPU()->SetReg(0, 0777);
    { std::ofstream output(invalid, std::ios::binary); output << "bad"; }
    preview_snapshot::AsyncLoad load;
    EXPECT_FALSE(load.Start({}));
    ASSERT_TRUE(load.Start(invalid));
    auto invalidResult = load.Finish();
    ASSERT_TRUE(invalidResult);
    EXPECT_FALSE(invalidResult->cancelled);
    EXPECT_FALSE(invalidResult->image);
    EXPECT_EQ(board->GetCPU()->GetReg(0), 0777);
    ASSERT_TRUE(session.Save(path));
    ASSERT_TRUE(load.Start(path));
    auto validResult = load.Finish();
    ASSERT_TRUE(validResult);
    ASSERT_FALSE(validResult->cancelled);
    ASSERT_TRUE(validResult->image);
    int stops = 0;
    ASSERT_TRUE(session.Restore(*validResult->image, [&] { ++stops; }));
    EXPECT_EQ(stops, 1);
}

TEST_F(PreviewSnapshotTest, CancelledAsyncLoadDiscardsResultAndRetainsReservationUntilCollected)
{
    preview_snapshot::Session session(*board);
    board->GetCPU()->SetReg(0, 012345);
    ASSERT_TRUE(session.Save(path));
    preview_snapshot::AsyncLoad load;
    load.Cancel();
    EXPECT_FALSE(load.Cancelled());
    ASSERT_TRUE(load.Start(path));
    load.Cancel();
    EXPECT_TRUE(load.Pending());
    EXPECT_TRUE(load.Cancelled());
    EXPECT_EQ(load.Path(), path);
    EXPECT_FALSE(load.Start(path));
    const auto cancelled = load.Finish();
    ASSERT_TRUE(cancelled);
    EXPECT_TRUE(cancelled->cancelled);
    EXPECT_FALSE(cancelled->image);
    EXPECT_FALSE(load.Pending());
    EXPECT_FALSE(load.Cancelled());
    EXPECT_TRUE(load.Path().empty());
    ASSERT_TRUE(load.Start(path));
    const auto retry = load.Finish();
    ASSERT_TRUE(retry);
    EXPECT_FALSE(retry->cancelled);
    ASSERT_TRUE(retry->image);
    board->GetCPU()->SetReg(0, 0777);
    ASSERT_TRUE(session.Restore(*retry->image, [] {}));
    EXPECT_EQ(board->GetCPU()->GetReg(0), 012345);
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
