/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "emubase/Emubase.h"
#include "FloppyReadTestHooks.h"
#include "FrameWorker.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <vector>

void DebugLog(const wchar_t*) {}

namespace {
class FloppyProbe : public CFloppyController {
public:
    auto& Raw() { return m_pDrive->data; }
    const auto& Markers() const { return m_pDrive->marker; }
    void Dirty() { m_trackchanged = true; }
    bool Changed() const { return m_trackchanged; }
    void CloseStream() { m_pDrive->fpFile->close(); }
    bool ReopenStream(const std::filesystem::path& path)
    {
        m_pDrive->fpFile->clear();
        m_pDrive->fpFile->open(path, std::ios::binary | std::ios::in | std::ios::out);
        return static_cast<bool>(*m_pDrive->fpFile);
    }
};
}

TEST(FloppyPortable, FailedFlushRetainsDirtyTrackAndReportsUntilRetried)
{
    const auto path = std::filesystem::temp_directory_path() /
        ("ukncbtl-floppy-error-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".dsk");
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove(path, ec); } } cleanup{path};
    { std::ofstream file(path, std::ios::binary); const std::vector<char> bytes(5120, 0); ASSERT_TRUE(file.write(bytes.data(), bytes.size())); }
    FloppyProbe disk;
    ASSERT_TRUE(disk.AttachImage(0, path));
    const auto goodTrack = disk.Raw();
    disk.Raw().fill(0);
    disk.Dirty();
    auto prepared = CFloppyController::OpenImage(path, true);
    ASSERT_TRUE(prepared);
    FrameWorker worker([&] { disk.Periodic(); return true; });
    ASSERT_TRUE(worker.Start([&] { return disk.AttachPreparedImage(1, std::move(*prepared)); }));
    EXPECT_EQ(worker.WaitOutcome(), FrameWorker::Outcome::stopped);
    EXPECT_FALSE(disk.IsAttached(1));
    EXPECT_TRUE(disk.Changed());
    EXPECT_TRUE(disk.IsAttached(0));
    const auto invalid = disk.TakeFailure();
    ASSERT_TRUE(invalid);
    EXPECT_EQ(invalid->error, FloppyError::invalidTrack);
    EXPECT_EQ(invalid->slot, 0);
    EXPECT_FALSE(disk.TakeFailure());
    disk.Raw() = goodTrack;
    disk.CloseStream();
    ASSERT_TRUE(worker.Start([&] { return disk.Flush(); }));
    EXPECT_EQ(worker.WaitOutcome(), FrameWorker::Outcome::stopped);
    EXPECT_TRUE(disk.Changed());
    const auto write = disk.TakeFailure();
    ASSERT_TRUE(write);
    EXPECT_EQ(write->error, FloppyError::writeFailed);
    ASSERT_TRUE(disk.ReopenStream(path));
    ASSERT_TRUE(worker.Start([&] { return disk.Flush(); }));
    EXPECT_EQ(worker.WaitOutcome(), FrameWorker::Outcome::completed);
    EXPECT_FALSE(disk.Changed());
    EXPECT_FALSE(disk.TakeFailure());
}

TEST(FloppyPortable, ReadFailureReportsAndProtectsTheDrive)
{
    const auto path = std::filesystem::temp_directory_path() /
        ("ukncbtl-floppy-read-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".dsk");
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove(path, ec); } } cleanup{path};
    { std::ofstream file(path, std::ios::binary); const std::vector<char> bytes(5120, 0); ASSERT_TRUE(file.write(bytes.data(), bytes.size())); }
    FloppyProbe disk;
    ASSERT_TRUE(disk.AttachImage(0, path));
    disk.CloseStream();
    disk.Reset();
    const auto failure = disk.TakeFailure();
    ASSERT_TRUE(failure);
    EXPECT_EQ(failure->error, FloppyError::readFailed);
    EXPECT_EQ(failure->slot, 0);
    EXPECT_TRUE(disk.IsReadOnly(0));
}

TEST(FloppyPortable, ExplicitReadOnlyMountProtectsAFileThatIsWritable)
{
    const auto path = std::filesystem::temp_directory_path() /
        ("ukncbtl-floppy-readonly-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".dsk");
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove(path, ec); } } cleanup{path};
    const std::vector<char> original(5120, 0x35);
    { std::ofstream file(path, std::ios::binary); ASSERT_TRUE(file.write(original.data(), original.size())); }
    FloppyProbe disk;
    ASSERT_TRUE(disk.AttachImage(0, path, true));
    EXPECT_TRUE(disk.IsReadOnly(0));
    disk.SetCommand(02003 | FLOPPY_CMD_ENGINESTART);
    EXPECT_NE(disk.GetState() & FLOPPY_STATUS_WRITEPROTECT, 0);
    disk.WriteData(0x1234);
    for (int index = 0; index < 100; ++index) disk.Periodic();
    EXPECT_FALSE(disk.Changed());
    disk.DetachImage(0);
    std::ifstream file(path, std::ios::binary);
    const std::vector<char> bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    EXPECT_EQ(bytes, original);
}

TEST(FloppyPortable, UnicodeRtdImagePreservesHeaderAndWritesTrack)
{
    const auto suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    auto path = std::filesystem::temp_directory_path() / ("ukncbtl-floppy-" + suffix);
    path += std::filesystem::path(u8"-\u65e5\u672c.RTD");
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() { std::error_code error; std::filesystem::remove(path, error); }
    } cleanup{path};

    std::vector<uint8_t> image(256 + 5120, 0x35);
    std::fill_n(image.begin(), 256, 0xa5);
    std::ofstream output(path, std::ios::binary);
    ASSERT_TRUE(output.write(reinterpret_cast<const char*>(image.data()), image.size()));
    output.close();
    ASSERT_TRUE(output);

    FloppyProbe disk;
    ASSERT_FALSE(disk.AttachImage(0, std::filesystem::path{}));
    ASSERT_FALSE(disk.AttachImage(-1, path));
    auto prepared = CFloppyController::OpenImage(path, false);
    ASSERT_TRUE(prepared);
    ASSERT_TRUE(disk.AttachPreparedImage(0, std::move(*prepared)));
    disk.SetCommand(02003 | FLOPPY_CMD_ENGINESTART);
    ASSERT_EQ(disk.Raw()[96], 0x35);
    disk.Raw()[96] = 0x7a;
    disk.Dirty();
    disk.Reset();
    ASSERT_FALSE(disk.Changed());
    disk.DetachImage(0);

    std::ifstream input(path, std::ios::binary);
    std::vector<uint8_t> actual(image.size());
    ASSERT_TRUE(input.read(reinterpret_cast<char*>(actual.data()), actual.size()));
    image[256] = 0x7a;
    EXPECT_EQ(actual, image);
}

namespace
{
class FloppyReadAheadTest : public testing::Test
{
protected:
    static constexpr int TrackBytes = 5120;
    static constexpr int FirstDataByte = 96;
    std::filesystem::path path;

    void SetUp() override
    {
        path = std::filesystem::temp_directory_path() /
            ("ukncbtl-floppy-prefetch-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".RTD");
        std::vector<std::uint8_t> bytes(256 + 4 * TrackBytes + 37, 0xa5);
        for (int side = 0; side < 5; ++side)
            std::fill_n(bytes.begin() + 256 + side * TrackBytes, side < 4 ? TrackBytes : 37,
                static_cast<std::uint8_t>(0x21 + side));
        std::ofstream file(path, std::ios::binary);
        ASSERT_TRUE(file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    }

    void TearDown() override
    {
        floppy_testing::ObserveReadAhead(nullptr);
        std::error_code error;
        std::filesystem::remove(path, error);
    }

    bool Attach(FloppyProbe& disk, int slot, bool readOnly = false) const
    {
        auto image = CFloppyController::OpenImage(path, readOnly);
        if (!image) return false;
        image->readAhead = true;
        return disk.AttachPreparedImage(slot, std::move(*image));
    }

    struct Gate
    {
        std::promise<void> entered;
        std::shared_future<void> proceed;
    };

    static bool HoldFirstPrefetch(void* context, long offset, bool speculative)
    {
        if (!speculative || offset != 256 + TrackBytes) return true;
        auto& gate = *static_cast<Gate*>(context);
        gate.entered.set_value();
        return gate.proceed.wait_for(std::chrono::seconds(3)) == std::future_status::ready;
    }
};
}

TEST_F(FloppyReadAheadTest, SideChangesAndSequentialStepsUsePrefetchedDataAndMarkers)
{
    constexpr std::array<std::uint16_t, 4> commands{FLOPPY_CMD_SIDEUP, 0,
        FLOPPY_CMD_STEP | FLOPPY_CMD_DIR, FLOPPY_CMD_STEP | FLOPPY_CMD_DIR};
    struct TrackState
    {
        std::array<std::uint8_t, FLOPPY_RAWTRACKSIZE> raw;
        std::array<std::uint8_t, FLOPPY_RAWMARKERSIZE> markers;
        std::uint16_t state, word;
    };
    std::vector<TrackState> expected;
    {
        FloppyProbe synchronous;
        ASSERT_TRUE(synchronous.AttachImage(0, path, true));
        for (const auto command : commands)
        {
            synchronous.SetCommand(command);
            synchronous.Periodic();
            expected.push_back({synchronous.Raw(), synchronous.Markers(), synchronous.GetState(), synchronous.GetData()});
        }
        EXPECT_FALSE(synchronous.TakeFailure());
    }

    struct Reads
    {
        std::array<std::atomic<int>, 8> demand{};
        std::array<std::atomic<int>, 8> speculative{};
    } reads;
    const floppy_testing::ReadObserver observer{&reads, [](void* context, long offset, bool speculative) {
        auto& reads = *static_cast<Reads*>(context);
        if (offset < 256 || (offset - 256) % TrackBytes != 0) return false;
        const auto index = static_cast<std::size_t>((offset - 256) / TrackBytes);
        if (index >= reads.demand.size()) return false;
        ++(speculative ? reads.speculative : reads.demand)[index];
        return true;
    }};
    FloppyProbe disk;
    floppy_testing::ObserveReadAhead(&observer);
    ASSERT_TRUE(Attach(disk, 0, true));
    for (std::size_t i = 0; i < commands.size(); ++i)
    {
        disk.SetCommand(commands[i]);
        disk.Periodic();
        EXPECT_EQ(disk.Raw(), expected[i].raw);
        EXPECT_EQ(disk.Markers(), expected[i].markers);
        EXPECT_EQ(disk.GetState(), expected[i].state);
        EXPECT_EQ(disk.GetData(), expected[i].word);
    }
    EXPECT_FALSE(disk.TakeFailure()); // The last requested side has a short payload.
    disk.DrainReadAhead();
    EXPECT_EQ(reads.demand[1].load(), 0);
    EXPECT_EQ(reads.speculative[1].load(), 2);
    EXPECT_EQ(reads.demand[2].load(), 1);
    EXPECT_EQ(reads.demand[4].load(), 0);
    EXPECT_EQ(reads.speculative[4].load(), 1);
}

TEST_F(FloppyReadAheadTest, FailedPrefetchRetriesAtTrackSelectionAndOnlyDemandFailureIsReported)
{
    int firstSideReads = 0; // Demand callbacks run on this thread.
    const floppy_testing::ReadObserver observer{&firstSideReads, [](void* context, long offset, bool speculative) {
        if (speculative) return false;
        return offset != 256 || ++*static_cast<int*>(context) == 1;
    }};
    FloppyProbe disk;
    floppy_testing::ObserveReadAhead(&observer);
    ASSERT_TRUE(Attach(disk, 0));
    EXPECT_FALSE(disk.TakeFailure());
    disk.SetCommand(FLOPPY_CMD_SIDEUP);
    EXPECT_EQ(disk.Raw()[FirstDataByte], 0x22);
    EXPECT_FALSE(disk.TakeFailure());
    EXPECT_FALSE(disk.IsReadOnly(0));
    disk.SetCommand(0);
    const auto failure = disk.TakeFailure();
    ASSERT_TRUE(failure);
    EXPECT_EQ(failure->error, FloppyError::readFailed);
    EXPECT_EQ(failure->slot, 0);
    EXPECT_TRUE(disk.IsReadOnly(0));
    EXPECT_FALSE(disk.TakeFailure());
}

TEST_F(FloppyReadAheadTest, ResetReloadsTheFirstSideEvenWhenItWasPredicted)
{
    struct Reads
    {
        int demand = 0;
        std::atomic<int> speculative{0};
    } reads;
    const floppy_testing::ReadObserver observer{&reads, [](void* context, long offset, bool speculative) {
        auto& reads = *static_cast<Reads*>(context);
        if (offset == 256)
        {
            if (speculative) ++reads.speculative;
            else ++reads.demand;
        }
        return true;
    }};
    FloppyProbe disk;
    floppy_testing::ObserveReadAhead(&observer);
    ASSERT_TRUE(Attach(disk, 0, true));
    disk.SetCommand(FLOPPY_CMD_SIDEUP);
    EXPECT_EQ(disk.Raw()[FirstDataByte], 0x22);
    disk.Reset();
    EXPECT_EQ(disk.Raw()[FirstDataByte], 0x21);
    EXPECT_EQ(reads.speculative.load(), 1);
    EXPECT_EQ(reads.demand, 2);
    EXPECT_FALSE(disk.TakeFailure());
}

TEST_F(FloppyReadAheadTest, DirtyFlushWaitsForTheReaderAndPreservesTheRtdHeader)
{
    std::promise<void> release;
    Gate gate{{}, release.get_future().share()};
    auto entered = gate.entered.get_future();
    const floppy_testing::ReadObserver observer{&gate, HoldFirstPrefetch};
    FloppyProbe disk;
    floppy_testing::ObserveReadAhead(&observer);
    ASSERT_TRUE(Attach(disk, 0));
    EXPECT_EQ(entered.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    disk.Raw()[FirstDataByte] = 0x7a;
    disk.Dirty();
    auto flushed = std::async(std::launch::async, [&] { return disk.Flush(); });
    EXPECT_EQ(flushed.wait_for(std::chrono::milliseconds(20)), std::future_status::timeout);
    release.set_value();
    EXPECT_TRUE(flushed.get());
    EXPECT_FALSE(disk.Changed());
    EXPECT_FALSE(disk.TakeFailure());
    disk.DetachImage(0);
    std::ifstream file(path, std::ios::binary);
    std::array<unsigned char, 257> bytes{};
    ASSERT_TRUE(file.read(reinterpret_cast<char*>(bytes.data()), bytes.size()));
    EXPECT_TRUE(std::all_of(bytes.begin(), bytes.begin() + 256, [](auto value) { return value == 0xa5; }));
    EXPECT_EQ(bytes.back(), 0x7a);
}

TEST_F(FloppyReadAheadTest, AnotherDriveCanAdvanceWhileTheFirstReadsAndEjectWaits)
{
    std::promise<void> release;
    Gate gate{{}, release.get_future().share()};
    auto entered = gate.entered.get_future();
    const floppy_testing::ReadObserver observer{&gate, HoldFirstPrefetch};
    FloppyProbe disk;
    floppy_testing::ObserveReadAhead(&observer);
    ASSERT_TRUE(Attach(disk, 1, true)); // Drive 0 is still selected and empty.
    ASSERT_TRUE(Attach(disk, 0, true));
    EXPECT_EQ(entered.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    FrameWorker owner([&] {
        disk.SetCommand(02002 | FLOPPY_CMD_SIDEUP); // Select drive 1, upper side.
        disk.Periodic();
        return disk.Raw()[FirstDataByte] == 0x22;
    });
    ASSERT_TRUE(owner.Start());
    const bool otherDriveProgressed = owner.WaitFor(std::chrono::milliseconds(500));
    EXPECT_TRUE(otherDriveProgressed);
    if (!otherDriveProgressed) release.set_value();
    EXPECT_EQ(owner.WaitOutcome(), FrameWorker::Outcome::completed);
    ASSERT_TRUE(owner.Start([&] { disk.DetachImage(0); return !disk.IsAttached(0); }));
    if (otherDriveProgressed)
    {
        EXPECT_FALSE(owner.WaitFor(std::chrono::milliseconds(20)));
        release.set_value();
    }
    EXPECT_EQ(owner.WaitOutcome(), FrameWorker::Outcome::completed);
    EXPECT_TRUE(disk.IsAttached(1));
    EXPECT_FALSE(disk.TakeFailure());
}
