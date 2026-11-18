/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "emubase/Emubase.h"
#include "FrameWorker.h"
#include <gtest/gtest.h>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
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
