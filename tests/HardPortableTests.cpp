/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "emubase/Emubase.h"
#include "AsyncDiskMount.h"
#include "FrameWorker.h"
#include "HardDiskTestHooks.h"
#include "MediaSlots.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <vector>

void DebugLog(const wchar_t*) {}

namespace
{
class FailingHardDrive : public CHardDrive
{
public:
    void CloseHostStream() { m_fpFile->close(); }
};
class TestBoard : public CMotherboard
{
public:
    FailingHardDrive& InstallDrive(int slot)
    {
        auto drive = std::make_unique<FailingHardDrive>();
        auto& result = *drive;
        m_pHardDrives[slot] = std::move(drive);
        return result;
    }
};
}

TEST(HardPortable, UnicodeImageSupportsSectorWriteAndRead)
{
    const auto suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    auto path = std::filesystem::temp_directory_path() / ("ukncbtl-hard-" + suffix);
    path += std::filesystem::path(u8"-\u65e5\u672c.img");
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() { std::error_code error; std::filesystem::remove(path, error); }
    } cleanup{path};

    std::vector<uint8_t> image(64 * 512, 0);
    image[0] = 8; image[1] = 2;
    std::ofstream output(path, std::ios::binary);
    ASSERT_TRUE(output.write(reinterpret_cast<const char*>(image.data()), image.size()));
    output.close();
    ASSERT_TRUE(output);

    CHardDrive disk;
    ASSERT_FALSE(disk.AttachImage(std::filesystem::path{}));
    ASSERT_TRUE(disk.AttachImage(path));
    disk.Reset();
    for (int i = 0; i < 2; ++i) disk.Periodic();
    const auto selectSecondSector = [&] {
        disk.WritePort(0x1f4, 0); disk.WritePort(0x1f5, 0);
        disk.WritePort(0x1f6, 0); disk.WritePort(0x1f3, 2); disk.WritePort(0x1f2, 1);
    };
    selectSecondSector();
    disk.WritePort(0x1f7, 0x30);
    for (int i = 0; i < 256; ++i) disk.WritePort(0x1f0, 0x1234);
    for (int i = 0; i < 256; ++i) disk.Periodic();
    ASSERT_EQ(disk.ReadPort(0x1f7) & 0x89, 0);

    selectSecondSector();
    disk.WritePort(0x1f7, 0x20);
    for (int i = 0; i < 768; ++i) disk.Periodic();
    ASSERT_EQ(disk.ReadPort(0x1f7) & 0x89, 8);
    for (int i = 0; i < 256; ++i) EXPECT_EQ(disk.ReadPort(0x1f0), 0x1234);
    disk.DetachImage();
    std::ifstream beforeFile(path, std::ios::binary);
    const std::vector<char> before{std::istreambuf_iterator<char>(beforeFile), std::istreambuf_iterator<char>()};
    beforeFile.close();
    ASSERT_TRUE(disk.AttachImage(path, true));
    EXPECT_TRUE(disk.IsReadOnly());
    disk.Reset();
    for (int i = 0; i < 2; ++i) disk.Periodic();
    selectSecondSector();
    disk.WritePort(0x1f7, 0x30);
    for (int i = 0; i < 256; ++i) disk.WritePort(0x1f0, 0x5678);
    for (int i = 0; i < 256; ++i) disk.Periodic();
    EXPECT_NE(disk.ReadPort(0x1f7) & 0x01, 0); // IDE status error bit.
    EXPECT_FALSE(disk.TakeHostFailure()); // A guest write to read-only media is not a host failure.
    disk.DetachImage();
    std::ifstream afterFile(path, std::ios::binary);
    const std::vector<char> after{std::istreambuf_iterator<char>(afterFile), std::istreambuf_iterator<char>()};
    EXPECT_EQ(after, before);
}

TEST(HardPortable, ReportsHostFailuresOnceAndKeepsThemAcrossDetach)
{
    const auto suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto path = std::filesystem::temp_directory_path() / ("ukncbtl-hard-errors-" + suffix + ".img");
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() { std::error_code error; std::filesystem::remove(path, error); }
    } cleanup{path};
    std::vector<uint8_t> image(64 * 512, 0);
    image[0] = 8; image[1] = 2;
    { std::ofstream output(path, std::ios::binary); ASSERT_TRUE(output.write(reinterpret_cast<const char*>(image.data()), image.size())); }
    CProcessor::Init();
    struct ProcessorCleanup { ~ProcessorCleanup() { CProcessor::Done(); } } processorCleanup;
    for (int slot = 0; slot < 2; ++slot)
        for (const bool writing : {false, true})
        {
            TestBoard board;
            auto& disk = board.InstallDrive(slot);
            ASSERT_TRUE(disk.AttachImage(path));
            disk.Reset();
            for (int i = 0; i < 2; ++i) disk.Periodic();
            disk.WritePort(0x1f4, 0); disk.WritePort(0x1f5, 0);
            disk.WritePort(0x1f6, 0); disk.WritePort(0x1f3, 0); disk.WritePort(0x1f2, 1);
            disk.WritePort(0x1f7, 0x20);
            for (int i = 0; i < 768; ++i) disk.Periodic();
            EXPECT_NE(disk.ReadPort(0x1f7) & 0x01, 0);
            EXPECT_FALSE(board.TakeHardDiskFailure());
            disk.WritePort(0x1f4, 0); disk.WritePort(0x1f5, 0);
            disk.WritePort(0x1f6, 0); disk.WritePort(0x1f3, 2); disk.WritePort(0x1f2, 1);
            disk.WritePort(0x1f7, writing ? 0x30 : 0x20);
            if (writing) for (int i = 0; i < 256; ++i) disk.WritePort(0x1f0, 0x1234);
            disk.CloseHostStream();
            for (int i = 0; i < 768; ++i) disk.Periodic();
            EXPECT_NE(disk.ReadPort(0x1f7) & 0x01, 0);
            if (slot == 1) board.DetachHardImage(slot + 1);
            const auto failure = board.TakeHardDiskFailure();
            ASSERT_TRUE(failure);
            EXPECT_EQ(failure->slot, slot);
            EXPECT_EQ(failure->error, writing ? HardDiskError::writeFailed : HardDiskError::readFailed);
            EXPECT_FALSE(board.TakeHardDiskFailure());
        }
}

TEST(HardPortable, WorkerFramesPublishHostReadAndWriteFailuresOnce)
{
    const auto suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto path = std::filesystem::temp_directory_path() / ("ukncbtl-worker-hard-error-" + suffix + ".img");
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code error; std::filesystem::remove(path, error); } } cleanup{path};
    std::vector<uint8_t> image(64 * 512, 0);
    image[0] = 8; image[1] = 2;
    { std::ofstream output(path, std::ios::binary); ASSERT_TRUE(output.write(reinterpret_cast<const char*>(image.data()), image.size())); }
    CProcessor::Init();
    struct ProcessorCleanup { ~ProcessorCleanup() { CProcessor::Done(); } } processorCleanup;
    for (int slot = 0; slot < 2; ++slot)
        for (const bool writing : {false, true})
        {
            TestBoard board;
            board.Reset();
            board.GetCPU()->SetDCLOPin(true);
            board.GetPPU()->SetDCLOPin(true);
            auto& disk = board.InstallDrive(slot);
            ASSERT_TRUE(disk.AttachImage(path));
            disk.Reset();
            FrameWorker worker([&] { return board.SystemFrame(); });
            ASSERT_TRUE(worker.Start());
            ASSERT_TRUE(worker.Wait());
            disk.WritePort(0x1f4, 0); disk.WritePort(0x1f5, 0);
            disk.WritePort(0x1f6, 0); disk.WritePort(0x1f3, 2); disk.WritePort(0x1f2, 1);
            disk.WritePort(0x1f7, writing ? 0x30 : 0x20);
            if (writing) for (int i = 0; i < 256; ++i) disk.WritePort(0x1f0, 0x1234);
            disk.CloseHostStream();
            ASSERT_TRUE(worker.Start());
            ASSERT_TRUE(worker.Wait());
            const auto failure = board.TakeHardDiskFailure();
            ASSERT_TRUE(failure);
            EXPECT_EQ(failure->slot, slot);
            EXPECT_EQ(failure->error, writing ? HardDiskError::writeFailed : HardDiskError::readFailed);
            EXPECT_FALSE(board.TakeHardDiskFailure());
        }
}

namespace
{
class HardReadAheadTest : public testing::Test
{
protected:
    std::filesystem::path path;

    void SetUp() override
    {
        path = std::filesystem::temp_directory_path() /
            ("ukncbtl-hard-prefetch-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".img");
        std::vector<std::uint8_t> bytes(64 * IDE_DISK_SECTOR_SIZE, 0);
        bytes[0] = 8;
        bytes[1] = 2;
        std::fill_n(bytes.begin() + IDE_DISK_SECTOR_SIZE, IDE_DISK_SECTOR_SIZE, 0x35);
        std::fill_n(bytes.begin() + 2 * IDE_DISK_SECTOR_SIZE, IDE_DISK_SECTOR_SIZE, 0x79);
        std::ofstream file(path, std::ios::binary);
        ASSERT_TRUE(file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    }

    void TearDown() override
    {
        hard_disk_testing::ObserveReadAhead(nullptr);
        std::error_code error;
        std::filesystem::remove(path, error);
    }

    static void Tick(CHardDrive& drive, int ticks)
    {
        for (int tick = 0; tick < ticks; ++tick) drive.Periodic();
    }

    static void Select(CHardDrive& drive, int sector, int count = 1)
    {
        drive.WritePort(0x1f4, 0);
        drive.WritePort(0x1f5, 0);
        drive.WritePort(0x1f6, 0);
        drive.WritePort(0x1f3, sector);
        drive.WritePort(0x1f2, count);
    }
};
}

TEST_F(HardReadAheadTest, FirstAndFollowingSectorsPublishAtTheirSeekDeadlines)
{
    struct Gate
    {
        std::promise<long> entered;
        std::shared_future<void> release;
    };
    std::promise<void> release;
    Gate gate{{}, release.get_future().share()};
    auto entered = gate.entered.get_future();
    const hard_disk_testing::ReadObserver observer{&gate, [](void* context, long offset) {
        auto& gate = *static_cast<Gate*>(context);
        if (offset != IDE_DISK_SECTOR_SIZE) return true;
        gate.entered.set_value(offset);
        return gate.release.wait_for(std::chrono::seconds(3)) == std::future_status::ready;
    }};
    CHardDrive drive;
    ASSERT_TRUE(drive.AttachImage(path, true));
    drive.EnableReadAhead();
    hard_disk_testing::ObserveReadAhead(&observer);
    Select(drive, 2, 2);
    drive.WritePort(0x1f7, 0x20);
    EXPECT_EQ(entered.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    Tick(drive, 767);
    EXPECT_EQ(drive.ReadPort(0x1f7) & 0x89, 0x80);
    EXPECT_FALSE(drive.TakeHostFailure());
    release.set_value();
    Tick(drive, 1);
    EXPECT_EQ(drive.ReadPort(0x1f7) & 0x89, 0x08);
    for (int word = 0; word < 256; ++word) EXPECT_EQ(drive.ReadPort(0x1f0), 0xcaca);
    Tick(drive, 511);
    EXPECT_EQ(drive.ReadPort(0x1f7) & 0x89, 0x80);
    Tick(drive, 1);
    EXPECT_EQ(drive.ReadPort(0x1f7) & 0x89, 0x08);
    for (int word = 0; word < 256; ++word) EXPECT_EQ(drive.ReadPort(0x1f0), 0x8686);
    EXPECT_EQ(drive.ReadPort(0x1f7) & 0x89, 0);
}

TEST_F(HardReadAheadTest, RedirectedSeekDiscardsStaleDataAndErrors)
{
    const hard_disk_testing::ReadObserver observer{nullptr, [](void*, long offset) {
        return offset != IDE_DISK_SECTOR_SIZE;
    }};
    CHardDrive drive;
    ASSERT_TRUE(drive.AttachImage(path, true));
    drive.EnableReadAhead();
    hard_disk_testing::ObserveReadAhead(&observer);
    Select(drive, 2);
    drive.WritePort(0x1f7, 0x20);
    drive.WritePort(0x1f3, 3); // Change CHS before the requested read completes.
    Tick(drive, 768);
    EXPECT_EQ(drive.ReadPort(0x1f7) & 0x89, 0x08);
    EXPECT_FALSE(drive.TakeHostFailure());
    for (int word = 0; word < 256; ++word) EXPECT_EQ(drive.ReadPort(0x1f0), 0x8686);

    Select(drive, 2);
    drive.WritePort(0x1f7, 0x20);
    Tick(drive, 767);
    EXPECT_FALSE(drive.TakeHostFailure());
    Tick(drive, 1);
    EXPECT_EQ(drive.ReadPort(0x1f7) & 0x89, 0x01);
    EXPECT_EQ(drive.TakeHostFailure(), HardDiskError::readFailed);
    EXPECT_FALSE(drive.TakeHostFailure());
}

TEST_F(HardReadAheadTest, ResetAbandonsAReadAndWriteCommitsInGuestOrder)
{
    struct Gate
    {
        std::promise<void> entered;
        std::shared_future<void> release;
    };
    std::promise<void> release;
    Gate gate{{}, release.get_future().share()};
    auto entered = gate.entered.get_future();
    const hard_disk_testing::ReadObserver observer{&gate, [](void* context, long) {
        auto& gate = *static_cast<Gate*>(context);
        gate.entered.set_value();
        (void)gate.release.wait_for(std::chrono::seconds(3));
        return false;
    }};
    CHardDrive drive;
    ASSERT_TRUE(drive.AttachImage(path));
    drive.EnableReadAhead();
    hard_disk_testing::ObserveReadAhead(&observer);
    Select(drive, 2);
    drive.WritePort(0x1f7, 0x20);
    EXPECT_EQ(entered.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    drive.Reset();
    Tick(drive, 2);
    EXPECT_EQ(drive.ReadPort(0x1f7) & 0x89, 0);

    Select(drive, 2);
    drive.WritePort(0x1f7, 0x30);
    for (int word = 0; word < 256; ++word) drive.WritePort(0x1f0, 0x1234);
    Tick(drive, 255);
    EXPECT_EQ(drive.ReadPort(0x1f7) & 0x89, 0x80);
    EXPECT_FALSE(drive.TakeHostFailure());
    release.set_value();
    Tick(drive, 1);
    EXPECT_EQ(drive.ReadPort(0x1f7) & 0x89, 0);
    EXPECT_FALSE(drive.TakeHostFailure());

    hard_disk_testing::ObserveReadAhead(nullptr);
    Select(drive, 2);
    drive.WritePort(0x1f7, 0x20);
    Tick(drive, 768);
    ASSERT_EQ(drive.ReadPort(0x1f7) & 0x89, 0x08);
    for (int word = 0; word < 256; ++word) EXPECT_EQ(drive.ReadPort(0x1f0), 0x1234);
}

TEST_F(HardReadAheadTest, SDLPreparedMountsPrefetchIndependentlyInAWorkerOwnedFrame)
{
    CProcessor::Init();
    struct ProcessorCleanup { ~ProcessorCleanup() { CProcessor::Done(); } } processorCleanup;
    struct Gate
    {
        std::array<std::promise<void>, 2> entered;
        std::shared_future<void> release;
    };
    std::promise<void> release;
    Gate gate{{}, release.get_future().share()};
    const hard_disk_testing::ReadObserver observer{&gate, [](void* context, long position) {
        auto& gate = *static_cast<Gate*>(context);
        const auto index = position / IDE_DISK_SECTOR_SIZE - 1;
        if (index < 0 || index >= static_cast<long>(gate.entered.size())) return false;
        gate.entered[index].set_value();
        return gate.release.wait_for(std::chrono::seconds(3)) == std::future_status::ready;
    }};
    CMotherboard board;
    board.Reset();
    board.GetCPU()->SetDCLOPin(true);
    board.GetPPU()->SetDCLOPin(true);
    const std::array<std::uint8_t, 24 * 1024> cartridge{};
    media_slots::Slots media(board);
    media_slots::AsyncDiskMount mount;
    for (int slot = 0; slot < 2; ++slot)
    {
        board.LoadROMCartridge(slot + 1, cartridge);
        ASSERT_TRUE(mount.Start(media_slots::Kind::hardDisk, path, slot, true));
        const auto mounted = mount.Finish(media);
        ASSERT_TRUE(mounted && *mounted);
    }
    hard_disk_testing::ObserveReadAhead(&observer);
    const auto write = [&](int drive, std::uint16_t port, std::uint16_t value) {
        board.SetHardPortWord(drive, static_cast<std::uint16_t>(2 * (7 - (port & 7))), static_cast<std::uint16_t>(~value));
    };
    FrameWorker frame([&] {
        for (int drive = 1; drive <= 2; ++drive)
        {
            write(drive, 0x1f3, static_cast<std::uint16_t>(drive + 1));
            write(drive, 0x1f2, 1);
            write(drive, 0x1f7, 0x20);
        }
        return board.SystemFrame();
    });
    ASSERT_TRUE(frame.Start());
    for (auto& read : gate.entered)
        EXPECT_EQ(read.get_future().wait_for(std::chrono::seconds(1)), std::future_status::ready);
    release.set_value();
    ASSERT_EQ(frame.WaitOutcome(), FrameWorker::Outcome::completed);
    EXPECT_EQ(board.GetHardPortWord(1, 14), 0x3535);
    EXPECT_EQ(board.GetHardPortWord(2, 14), 0x7979);
    EXPECT_TRUE(media.Eject(media_slots::Kind::hardDisk, 0));
    EXPECT_TRUE(media.Eject(media_slots::Kind::hardDisk, 1));
}

TEST_F(HardReadAheadTest, DetachWaitsForTheOutstandingReadAndAllowsReattachment)
{
    struct Gate
    {
        std::promise<void> entered;
        std::shared_future<void> release;
    };
    std::promise<void> release;
    Gate gate{{}, release.get_future().share()};
    auto entered = gate.entered.get_future();
    const hard_disk_testing::ReadObserver observer{&gate, [](void* context, long) {
        auto& gate = *static_cast<Gate*>(context);
        gate.entered.set_value();
        return gate.release.wait_for(std::chrono::seconds(3)) == std::future_status::ready;
    }};
    CHardDrive drive;
    ASSERT_TRUE(drive.AttachImage(path, true));
    drive.EnableReadAhead();
    hard_disk_testing::ObserveReadAhead(&observer);
    Select(drive, 2);
    drive.WritePort(0x1f7, 0x20);
    EXPECT_EQ(entered.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    auto detached = std::async(std::launch::async, [&] { drive.DetachImage(); });
    EXPECT_EQ(detached.wait_for(std::chrono::milliseconds(20)), std::future_status::timeout);
    release.set_value();
    detached.get();
    EXPECT_FALSE(drive.TakeHostFailure());

    hard_disk_testing::ObserveReadAhead(nullptr);
    ASSERT_TRUE(drive.AttachImage(path, true));
    Select(drive, 3);
    drive.WritePort(0x1f7, 0x20);
    Tick(drive, 768);
    EXPECT_EQ(drive.ReadPort(0x1f7) & 0x89, 0x08);
    EXPECT_EQ(drive.ReadPort(0x1f0), 0x8686);
}
