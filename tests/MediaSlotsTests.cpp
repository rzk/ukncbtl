/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "MediaSlots.h"
#include "AsyncDiskMount.h"
#include "FloppyReadTestHooks.h"
#include "FrameWorker.h"
#include "PreviewMediaRestore.h"
#include "PreviewRoms.h"
#include "PreviewTape.h"
#include "Snapshot.h"
#include "emubase/Emubase.h"
#include <gtest/gtest.h>
#include <chrono>
#include <array>
#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <future>
#include <iterator>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

void DebugLog(const wchar_t*) {}

namespace
{
class MediaSlotsTest : public testing::Test
{
protected:
    std::filesystem::path directory, floppy, hard;
    void SetUp() override
    {
        CProcessor::Init();
        directory = std::filesystem::temp_directory_path() /
            ("ukncbtl-media-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        ASSERT_TRUE(std::filesystem::create_directory(directory));
        floppy = directory / std::filesystem::path(u8"\u65e5\u672c.dsk");
        hard = directory / std::filesystem::path(u8"\u65e5\u672c.img");
        std::vector<std::uint8_t> data(64 * 512, 0);
        data[0] = 8; data[1] = 2;
        for (const auto& path : {floppy, hard})
        {
            std::ofstream output(path, std::ios::binary);
            ASSERT_TRUE(output.write(reinterpret_cast<const char*>(data.data()), data.size()));
        }
    }
    void TearDown() override
    {
        std::error_code error;
        std::filesystem::remove(floppy, error);
        std::filesystem::remove(hard, error);
        std::filesystem::remove(directory / "alias.img", error);
        std::filesystem::remove(directory / "startup.rom", error);
        std::filesystem::remove(directory / "startup.uknc", error);
        std::filesystem::remove(directory / "startup.wav", error);
        std::filesystem::remove(directory, error);
        CProcessor::Done();
    }
};

TEST_F(MediaSlotsTest, AttachesAndEjectsAllDriveSlots)
{
    CMotherboard board;
    board.Reset();
    media_slots::Slots media(board);
    for (int slot = 0; slot < 4; ++slot)
    {
        ASSERT_TRUE(media.Attach(media_slots::Kind::floppy, slot, floppy));
        EXPECT_TRUE(board.IsFloppyImageAttached(slot));
        ASSERT_NE(media.Path(media_slots::Kind::floppy, slot), nullptr);
        EXPECT_EQ(*media.Path(media_slots::Kind::floppy, slot), floppy);
        EXPECT_EQ(media.ReadOnly(media_slots::Kind::floppy, slot), board.IsFloppyReadOnly(slot));
        ASSERT_TRUE(media.Eject(media_slots::Kind::floppy, slot));
        EXPECT_FALSE(board.IsFloppyImageAttached(slot));
        EXPECT_EQ(media.Path(media_slots::Kind::floppy, slot), nullptr);
    }
    for (int slot = 0; slot < 2; ++slot)
    {
        const std::array<std::uint8_t, 24 * 1024> cartridge{};
        board.LoadROMCartridge(slot + 1, cartridge);
        ASSERT_TRUE(media.Attach(media_slots::Kind::hardDisk, slot, hard));
        EXPECT_TRUE(board.IsHardImageAttached(slot + 1));
        ASSERT_NE(media.Path(media_slots::Kind::hardDisk, slot), nullptr);
        EXPECT_EQ(*media.Path(media_slots::Kind::hardDisk, slot), hard);
        EXPECT_EQ(media.ReadOnly(media_slots::Kind::hardDisk, slot), board.IsHardImageReadOnly(slot + 1));
        ASSERT_TRUE(media.Eject(media_slots::Kind::hardDisk, slot));
        EXPECT_FALSE(board.IsHardImageAttached(slot + 1));
    }
}

TEST_F(MediaSlotsTest, PreparedFloppyMountEnablesReadAheadOnTheBoard)
{
    std::promise<long> firstRead;
    auto offset = firstRead.get_future();
    const floppy_testing::ReadObserver observer{&firstRead, [](void* context, long offset, bool speculative) {
        if (speculative) static_cast<std::promise<long>*>(context)->set_value(offset);
        return true;
    }};
    struct ResetObserver { ~ResetObserver() { floppy_testing::ObserveReadAhead(nullptr); } } resetObserver;
    floppy_testing::ObserveReadAhead(&observer);
    CMotherboard board;
    media_slots::Slots media(board);
    media_slots::AsyncDiskMount mount;
    ASSERT_TRUE(mount.Start(media_slots::Kind::floppy, floppy, 0, true));
    const auto mounted = mount.Finish(media);
    ASSERT_TRUE(mounted && *mounted);
    ASSERT_EQ(offset.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    EXPECT_EQ(offset.get(), 5120);
    board.DrainFloppyReadAhead();
    EXPECT_TRUE(media.Eject(media_slots::Kind::floppy, 0));
}

TEST_F(MediaSlotsTest, WorkerFramesKeepReadOnlyFloppyIntactThroughFlushAndEject)
{
    const auto readImage = [&] {
        std::ifstream input(floppy, std::ios::binary);
        return std::vector<char>(std::istreambuf_iterator<char>(input), {});
    };
    const auto original = readImage();
    ASSERT_FALSE(original.empty());
    CMotherboard board;
    board.Reset();
    board.GetCPU()->SetDCLOPin(true);
    board.GetPPU()->SetDCLOPin(true);
    media_slots::Slots media(board);
    media_slots::AsyncDiskMount mount;
    ASSERT_TRUE(mount.Start(media_slots::Kind::floppy, floppy, 0, true));
    FrameWorker worker([&] { return board.SystemFrame(); });
    ASSERT_TRUE(worker.Start());
    ASSERT_TRUE(worker.Wait());
    EXPECT_FALSE(media.Attached(media_slots::Kind::floppy, 0));
    ASSERT_TRUE(worker.Start([&] {
        const auto mounted = mount.Finish(media);
        return mounted && mounted->has_value();
    }));
    ASSERT_TRUE(worker.Wait());
    EXPECT_FALSE(mount.Ready());
    EXPECT_TRUE(media.RequestedReadOnly(media_slots::Kind::floppy, 0));
    board.SetFloppyState(02003 | FLOPPY_CMD_ENGINESTART);
    ASSERT_TRUE(board.IsFloppyEngineOn());
    board.SetFloppyData(0x1234);
    for (int frame = 0; frame < 5; ++frame)
    {
        ASSERT_TRUE(worker.Start());
        ASSERT_TRUE(worker.Wait());
    }
    EXPECT_TRUE(media.ReadOnly(media_slots::Kind::floppy, 0));
    ASSERT_TRUE(worker.Start([&] { return media.FlushFloppy(); }));
    EXPECT_TRUE(worker.Wait());
    EXPECT_FALSE(board.TakeFloppyFailure());
    ASSERT_TRUE(worker.Start([&] { return media.Eject(media_slots::Kind::floppy, 0).has_value(); }));
    ASSERT_TRUE(worker.Wait());
    EXPECT_FALSE(media.Attached(media_slots::Kind::floppy, 0));
    EXPECT_EQ(readImage(), original);
}

TEST_F(MediaSlotsTest, WorkerFrameCommitsHardDiskWriteBeforeEject)
{
    CMotherboard board;
    board.Reset();
    board.GetCPU()->SetDCLOPin(true);
    board.GetPPU()->SetDCLOPin(true);
    const std::array<std::uint8_t, 24 * 1024> cartridge{};
    board.LoadROMCartridge(1, cartridge);
    media_slots::Slots media(board);
    media_slots::AsyncDiskMount mount;
    ASSERT_TRUE(mount.Start(media_slots::Kind::hardDisk, hard, 0, false));
    EXPECT_EQ(mount.Path(), hard);
    EXPECT_FALSE(mount.Start(media_slots::Kind::floppy, floppy, 1, true));
    FrameWorker worker([&] { return board.SystemFrame(); });
    ASSERT_TRUE(worker.Start());
    ASSERT_TRUE(worker.Wait());
    EXPECT_FALSE(media.Attached(media_slots::Kind::hardDisk, 0));
    ASSERT_TRUE(worker.Start([&] {
        const auto mounted = mount.Finish(media);
        return mounted && mounted->has_value();
    }));
    ASSERT_TRUE(worker.Wait());
    EXPECT_FALSE(media.ReadOnly(media_slots::Kind::hardDisk, 0));
    ASSERT_TRUE(worker.Start());
    ASSERT_TRUE(worker.Wait()); // Complete the drive's reset delay.

    // The board maps and inverts the guest QBUS address and data bits.
    const auto writePort = [&](std::uint16_t ataPort, std::uint16_t value) {
        const auto qbusPort = static_cast<std::uint16_t>((~ataPort & 7) << 1);
        board.SetHardPortWord(1, qbusPort, static_cast<std::uint16_t>(~value));
    };
    writePort(0x1f4, 0); writePort(0x1f5, 0); writePort(0x1f6, 0);
    writePort(0x1f3, 2); writePort(0x1f2, 1); writePort(0x1f7, 0x30);
    for (int i = 0; i < 256; ++i) writePort(0x1f0, 0x1234);
    ASSERT_TRUE(worker.Start());
    ASSERT_TRUE(worker.Wait()); // Periodic drive ticks commit the sector.
    const auto status = static_cast<std::uint16_t>(~board.GetHardPortWord(1, 0));
    EXPECT_EQ(status & 0x89, 0);
    EXPECT_FALSE(board.TakeHardDiskFailure());
    ASSERT_TRUE(worker.Start([&] { return media.Eject(media_slots::Kind::hardDisk, 0).has_value(); }));
    ASSERT_TRUE(worker.Wait());
    EXPECT_FALSE(media.Attached(media_slots::Kind::hardDisk, 0));
    std::ifstream input(hard, std::ios::binary);
    input.seekg(512);
    std::array<unsigned char, 512> sector{};
    ASSERT_TRUE(input.read(reinterpret_cast<char*>(sector.data()), sector.size()));
    for (std::size_t i = 0; i < sector.size(); i += 2)
    { EXPECT_EQ(sector[i], 0xcb); EXPECT_EQ(sector[i + 1], 0xed); }
}

TEST_F(MediaSlotsTest, AsyncHardMountRechecksCartridgeAndConflicts)
{
    using media_slots::Error;
    using media_slots::Kind;
    CMotherboard board;
    board.Reset();
    const std::array<std::uint8_t, 24 * 1024> cartridge{};
    media_slots::Slots media(board);
    media_slots::AsyncDiskMount mount;
    board.LoadROMCartridge(1, cartridge);
    ASSERT_TRUE(media.CheckAttach(Kind::hardDisk, 0, hard, true));
    ASSERT_TRUE(mount.Start(Kind::hardDisk, hard, 0, true));
    board.UnloadROMCartridge(1);
    auto result = mount.Finish(media);
    ASSERT_TRUE(result && !*result);
    EXPECT_EQ(result->error(), Error::missing_cartridge);

    board.LoadROMCartridge(1, cartridge);
    ASSERT_TRUE(mount.Start(Kind::hardDisk, hard, 0, true));
    ASSERT_TRUE(media.Attach(Kind::floppy, 0, hard));
    result = mount.Finish(media);
    ASSERT_TRUE(result && !*result);
    EXPECT_EQ(result->error(), Error::already_mounted);
    EXPECT_FALSE(media.Attached(Kind::hardDisk, 0));
    ASSERT_TRUE(media.Eject(Kind::floppy, 0));

    ASSERT_TRUE(mount.Start(Kind::hardDisk, directory / "missing.img", 0, true));
    result = mount.Finish(media);
    ASSERT_TRUE(result && !*result);
    EXPECT_EQ(result->error(), Error::attach_failed);
    ASSERT_TRUE(mount.Start(Kind::hardDisk, hard, 0, true));
    result = mount.Finish(media);
    ASSERT_TRUE(result && *result);
    EXPECT_TRUE(media.RequestedReadOnly(Kind::hardDisk, 0));
    EXPECT_TRUE(media.ReadOnly(Kind::hardDisk, 0));
    EXPECT_FALSE(mount.Pending());
}

TEST_F(MediaSlotsTest, AsyncFloppyMountRechecksOccupancyAndCanRetry)
{
    using media_slots::Kind;
    CMotherboard board;
    board.Reset();
    media_slots::Slots media(board);
    media_slots::AsyncDiskMount mount;
    ASSERT_TRUE(mount.Start(Kind::floppy, floppy, 3, false));
    ASSERT_TRUE(media.Attach(Kind::floppy, 3, hard, true));
    auto result = mount.Finish(media);
    ASSERT_TRUE(result && !*result);
    EXPECT_EQ(result->error(), media_slots::Error::occupied);
    ASSERT_NE(media.Path(Kind::floppy, 3), nullptr);
    EXPECT_EQ(*media.Path(Kind::floppy, 3), hard);
    ASSERT_TRUE(media.Eject(Kind::floppy, 3));

    ASSERT_TRUE(mount.Start(Kind::floppy, directory / "missing.dsk", 3, false));
    result = mount.Finish(media);
    ASSERT_TRUE(result && !*result);
    EXPECT_EQ(result->error(), media_slots::Error::attach_failed);
    ASSERT_TRUE(mount.Start(Kind::floppy, floppy, 3, false));
    result = mount.Finish(media);
    ASSERT_TRUE(result && *result);
    EXPECT_FALSE(media.ReadOnly(Kind::floppy, 3));
}

TEST_F(MediaSlotsTest, DiscardJoinsPreparedMountWithoutInstallingIt)
{
    using media_slots::Kind;
    CMotherboard board;
    board.Reset();
    media_slots::Slots media(board);
    media_slots::AsyncDiskMount mount;
    ASSERT_TRUE(mount.Start(Kind::floppy, floppy, 0, false));
    mount.Discard();
    EXPECT_FALSE(mount.Pending());
    EXPECT_TRUE(mount.Path().empty());
    EXPECT_EQ(mount.Slot(), -1);
    EXPECT_FALSE(media.Attached(Kind::floppy, 0));
    ASSERT_TRUE(mount.Start(Kind::floppy, floppy, 0, true));
    const auto result = mount.Finish(media);
    ASSERT_TRUE(result && *result);
    EXPECT_TRUE(media.ReadOnly(Kind::floppy, 0));
}

TEST_F(MediaSlotsTest, StartupPreparationInstallsMediaInOrderAndLeavesStateForCaller)
{
    CMotherboard board;
    board.Reset();
    preview_roms::Roms roms(board);
    media_slots::Slots media(board);
    preview_tape::Session tape(board);
    const auto state = directory / "startup.uknc";
    board.GetCPU()->SetReg(0, 012345);
    ASSERT_TRUE(snapshot::Save(state, board, 0));
    board.GetCPU()->SetReg(0, 0777);

    const auto cartridge = directory / "startup.rom";
    {
        const std::array<char, 24 * 1024> bytes{};
        std::ofstream file(cartridge, std::ios::binary);
        ASSERT_TRUE(file.write(bytes.data(), bytes.size()));
    }
    preview_settings::Settings settings;
    settings.restoreMedia = true;
    settings.cartridges[0] = cartridge;
    settings.floppies[1] = directory / "missing.dsk";
    preview_options::Options options;
    options.state = state;
    options.hardDisks[0] = hard;
    options.hardDiskReadOnly[0] = true;
    const auto result = preview_media_restore::PrepareStartup(settings, options, roms, media, tape, {});
    ASSERT_TRUE(result) << result.error();
    EXPECT_EQ(result->restoreErrors, (std::vector<std::string>{"Floppy 2"}));
    EXPECT_FALSE(result->stateImage.empty());
    EXPECT_EQ(board.GetCPU()->GetReg(0), 0777);
    EXPECT_TRUE(roms.Inserted(0));
    EXPECT_TRUE(media.ReadOnly(media_slots::Kind::hardDisk, 0));
    EXPECT_EQ(settings.floppies[1], directory / "missing.dsk");
}

TEST_F(MediaSlotsTest, StartupReadsIndependentFilesTogetherAndBoundsWorkInFlight)
{
    const auto cartridge = directory / "startup.rom";
    {
        const std::array<char, 24 * 1024> bytes{};
        std::ofstream output(cartridge, std::ios::binary);
        ASSERT_TRUE(output.write(bytes.data(), bytes.size()));
    }
    CMotherboard board;
    preview_roms::Roms roms(board);
    media_slots::Slots media(board);
    preview_tape::Session tape(board);
    const auto state = directory / "startup.uknc";
    ASSERT_TRUE(snapshot::Save(state, board, 0));
    const auto wavePath = directory / "startup.wav";
    auto wave = WavPcmFile::Create(wavePath, 44100);
    ASSERT_TRUE(wave);
    ASSERT_TRUE(wave->WriteOne(0x80000000));
    ASSERT_TRUE(wave->Close());
    preview_options::Options options;
    options.cartridges[0] = cartridge;
    options.floppies[0] = floppy;
    options.hardDisks[0] = hard;
    options.hardDiskReadOnly[0] = true;
    options.tape = wavePath;
    options.state = state;

    struct Gate
    {
        std::mutex mutex;
        std::condition_variable changed;
        std::vector<std::filesystem::path> reads;
        bool release = false;
    } gate;
    using namespace preview_media_restore::testing;
    const ReadObserver observer{&gate, [](void* context, const std::filesystem::path& path) {
        auto& gate = *static_cast<Gate*>(context);
        std::unique_lock lock(gate.mutex);
        gate.reads.push_back(path);
        gate.changed.notify_all();
        (void)gate.changed.wait_for(lock, std::chrono::seconds(5), [&] { return gate.release; });
    }};
    struct RemoveObserver { ~RemoveObserver() { ObserveReads(nullptr); } } removeObserver;
    ObserveReads(&observer);
    auto work = std::async(std::launch::async, [&] {
        return preview_media_restore::PrepareStartup({}, options, roms, media, tape, {});
    });
    {
        std::unique_lock lock(gate.mutex);
        EXPECT_TRUE(gate.changed.wait_for(lock, std::chrono::seconds(2), [&] { return gate.reads.size() >= 3; }));
        EXPECT_FALSE(gate.changed.wait_for(lock, std::chrono::milliseconds(75), [&] { return gate.reads.size() > 3; }));
        EXPECT_EQ(gate.reads.size(), 3u);
        gate.release = true;
    }
    gate.changed.notify_all();
    const auto loaded = work.get();
    ASSERT_TRUE(loaded) << loaded.error();
    EXPECT_TRUE(loaded->restoreErrors.empty());
    EXPECT_FALSE(loaded->stateImage.empty());
    EXPECT_TRUE(roms.Inserted(0));
    EXPECT_TRUE(media.Attached(media_slots::Kind::floppy, 0));
    EXPECT_TRUE(media.ReadOnly(media_slots::Kind::hardDisk, 0));
    EXPECT_TRUE(tape.Inserted());
    EXPECT_FALSE(tape.Playing());
    EXPECT_EQ(tape.Length(), 1u);
    ASSERT_EQ(gate.reads.size(), 5u);
}

TEST_F(MediaSlotsTest, StartupRechecksAliasedDisksAfterTheEarlierMount)
{
    using Kind = media_slots::Kind;
    preview_options::Options options;
    options.floppies[0] = floppy;
    options.floppies[1] = directory / "." / floppy.filename();
    options.floppyReadOnly[1] = true;
    for (const bool firstReadOnly : {false, true})
    {
        CMotherboard board;
        preview_roms::Roms roms(board);
        media_slots::Slots media(board);
        preview_tape::Session tape(board);
        options.floppyReadOnly[0] = firstReadOnly;
        const auto result = preview_media_restore::PrepareStartup({}, options, roms, media, tape, {});
        EXPECT_EQ(result.has_value(), firstReadOnly);
        if (!result) EXPECT_EQ(result.error(), "Could not attach requested floppy 1.");
        EXPECT_TRUE(media.Attached(Kind::floppy, 0));
        EXPECT_EQ(media.Attached(Kind::floppy, 1), firstReadOnly);
    }
}

TEST_F(MediaSlotsTest, StartupFailureLeavesLaterPreparedMediaDetached)
{
    const auto invalidCartridge = directory / "startup.rom";
    { std::ofstream output(invalidCartridge, std::ios::binary); output << "short ROM"; }
    CMotherboard board;
    preview_roms::Roms roms(board);
    media_slots::Slots media(board);
    preview_tape::Session tape(board);
    preview_options::Options options;
    options.cartridges[0] = invalidCartridge;
    options.floppies[0] = floppy;
    const auto result = preview_media_restore::PrepareStartup({}, options, roms, media, tape, {});
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), "Could not load requested cartridge 1.");
    EXPECT_FALSE(roms.Inserted(0));
    EXPECT_FALSE(media.Attached(media_slots::Kind::floppy, 0));
    EXPECT_TRUE(media.Attach(media_slots::Kind::floppy, 0, floppy));
}

TEST_F(MediaSlotsTest, CancelingStartupDuringAReadDiscardsThePreparedMedia)
{
    CMotherboard board;
    preview_roms::Roms roms(board);
    media_slots::Slots media(board);
    preview_tape::Session tape(board);
    preview_options::Options options;
    options.floppies[0] = floppy;
    options.floppies[1] = hard;
    std::stop_source stop;
    using namespace preview_media_restore::testing;
    const ReadObserver observer{&stop, [](void* context, const std::filesystem::path&) {
        static_cast<std::stop_source*>(context)->request_stop();
    }};
    struct RemoveObserver { ~RemoveObserver() { ObserveReads(nullptr); } } removeObserver;
    ObserveReads(&observer);
    const auto result = preview_media_restore::PrepareStartup({}, options, roms, media, tape, {}, stop.get_token());
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), "Startup media loading was canceled.");
    EXPECT_FALSE(media.Attached(media_slots::Kind::floppy, 0));
    EXPECT_FALSE(media.Attached(media_slots::Kind::floppy, 1));
    EXPECT_TRUE(media.Attach(media_slots::Kind::floppy, 0, floppy));
}

TEST_F(MediaSlotsTest, StartupRejectsMissingCartridgeBeforeUsingThePreparedDisk)
{
    CMotherboard board;
    preview_roms::Roms roms(board);
    media_slots::Slots media(board);
    preview_tape::Session tape(board);
    preview_options::Options options;
    options.floppies[0] = floppy;
    options.hardDisks[0] = hard;
    struct FailingRead
    {
        std::filesystem::path path;
        std::atomic<bool> attempted{false};
    } failure{hard};
    using namespace preview_media_restore::testing;
    const ReadObserver observer{&failure, [](void* context, const std::filesystem::path& path) {
        auto& failure = *static_cast<FailingRead*>(context);
        if (path != failure.path) return;
        failure.attempted = true;
        throw std::runtime_error("Disk read failed");
    }};
    struct RemoveObserver { ~RemoveObserver() { ObserveReads(nullptr); } } removeObserver;
    ObserveReads(&observer);
    const auto result = preview_media_restore::PrepareStartup({}, options, roms, media, tape, {});
    ASSERT_FALSE(result);
    EXPECT_TRUE(failure.attempted.load());
    EXPECT_EQ(result.error(), "Could not attach requested hard disk 1; check its ROM cartridge and image.");
    EXPECT_TRUE(media.Attached(media_slots::Kind::floppy, 0));
    EXPECT_FALSE(media.Attached(media_slots::Kind::hardDisk, 0));
}

TEST_F(MediaSlotsTest, RestoresMediaInDependencyOrderAndPreservesFailedPaths)
{
    const auto cartridge = directory / "cartridge.rom";
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove(path, ec); } } cleanup{cartridge};
    { std::ofstream file(cartridge, std::ios::binary); const std::vector<char> bytes(24 * 1024); ASSERT_TRUE(file.write(bytes.data(), bytes.size())); }
    CMotherboard board;
    preview_roms::Roms roms(board);
    media_slots::Slots media(board);
    preview_settings::Settings settings;
    settings.restoreMedia = true;
    settings.cartridges[0] = cartridge;
    settings.floppies[0] = floppy;
    settings.floppyReadOnly[0] = true;
    settings.floppies[1] = directory / "missing.dsk";
    settings.hardDisks[0] = hard;
    settings.hardDiskReadOnly[0] = true;
    const auto errors = preview_media_restore::Restore(settings, {}, roms, media);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_EQ(errors[0], "Floppy 2");
    EXPECT_TRUE(roms.Inserted(0));
    EXPECT_TRUE(media.Attached(media_slots::Kind::floppy, 0));
    EXPECT_TRUE(media.Attached(media_slots::Kind::hardDisk, 0));
    EXPECT_TRUE(media.ReadOnly(media_slots::Kind::floppy, 0));
    EXPECT_TRUE(media.RequestedReadOnly(media_slots::Kind::floppy, 0));
    EXPECT_TRUE(media.ReadOnly(media_slots::Kind::hardDisk, 0));
    EXPECT_TRUE(media.RequestedReadOnly(media_slots::Kind::hardDisk, 0));
    EXPECT_EQ(settings.floppies[1], directory / "missing.dsk");
}

TEST_F(MediaSlotsTest, RestoreHonorsDisabledOptionAndCommandLineOverrides)
{
    CMotherboard board;
    preview_roms::Roms roms(board);
    media_slots::Slots media(board);
    preview_settings::Settings settings;
    settings.floppies[0] = directory / "missing.dsk";
    EXPECT_TRUE(preview_media_restore::Restore(settings, {}, roms, media).empty());
    settings.restoreMedia = true;
    preview_options::Options options;
    options.floppies[0] = floppy;
    ASSERT_TRUE(media.Attach(media_slots::Kind::floppy, 0, floppy));
    EXPECT_TRUE(preview_media_restore::Restore(settings, options, roms, media).empty());
    EXPECT_EQ(*media.Path(media_slots::Kind::floppy, 0), floppy);
    EXPECT_EQ(settings.floppies[0], directory / "missing.dsk");
    options.floppies[0].reset(); options.smokeTest = true;
    EXPECT_TRUE(preview_media_restore::Restore(settings, options, roms, media).empty());
}

TEST_F(MediaSlotsTest, MissingCartridgeReportsBothDependentDevices)
{
    CMotherboard board;
    preview_roms::Roms roms(board);
    media_slots::Slots media(board);
    preview_settings::Settings settings;
    settings.restoreMedia = true;
    settings.cartridges[1] = directory / "missing.rom";
    settings.hardDisks[1] = hard;
    const auto errors = preview_media_restore::Restore(settings, {}, roms, media);
    ASSERT_EQ(errors.size(), 2u);
    EXPECT_EQ(errors[0], "Cartridge 2");
    EXPECT_EQ(errors[1], "Hard disk 2");
    EXPECT_EQ(settings.hardDisks[1], hard);
    EXPECT_FALSE(media.Attached(media_slots::Kind::hardDisk, 1));
}

TEST_F(MediaSlotsTest, ExplicitStateTakesPriorityOverRememberedRomAndConflictingMedia)
{
    CMotherboard board;
    preview_roms::Roms roms(board);
    media_slots::Slots media(board);
    preview_settings::Settings settings;
    settings.restoreMedia = true;
    settings.systemRom = directory / "missing.rom";
    settings.floppies[0] = floppy;
    settings.cartridges[1] = floppy;
    preview_options::Options options;
    options.state = floppy;
    const auto errors = preview_media_restore::Restore(settings, options, roms, media);
    ASSERT_EQ(errors.size(), 2u);
    EXPECT_EQ(errors[0], "Cartridge 2");
    EXPECT_EQ(errors[1], "Floppy 1");
    EXPECT_FALSE(roms.Inserted(1));
    EXPECT_FALSE(media.Attached(media_slots::Kind::floppy, 0));
    EXPECT_EQ(settings.systemRom, directory / "missing.rom");
    EXPECT_EQ(settings.floppies[0], floppy);
}

TEST_F(MediaSlotsTest, MountedHardDiskPreventsEjectingItsCartridge)
{
    const auto cartridge = directory / "controller.rom";
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove(path, ec); } } cleanup{cartridge};
    { std::ofstream file(cartridge, std::ios::binary); const std::vector<char> bytes(24 * 1024); ASSERT_TRUE(file.write(bytes.data(), bytes.size())); }
    for (int slot = 0; slot < 2; ++slot)
        for (const bool readOnly : {false, true})
        {
            CMotherboard board;
            preview_roms::Roms roms(board);
            media_slots::Slots media(board);
            preview_settings::Settings settings;
            settings.restoreMedia = true;
            settings.cartridges[slot] = cartridge;
            settings.hardDisks[slot] = hard;
            settings.hardDiskReadOnly[slot] = readOnly;
            ASSERT_TRUE(preview_media_restore::Restore(settings, {}, roms, media).empty());
            ASSERT_TRUE(roms.Insert(1 - slot, cartridge));
            const auto blocked = roms.Eject(slot);
            ASSERT_FALSE(blocked);
            EXPECT_EQ(blocked.error(), preview_roms::Error::hard_disk_attached);
            EXPECT_TRUE(roms.Inserted(slot));
            EXPECT_EQ(*roms.CartridgePath(slot), cartridge);
            EXPECT_TRUE(media.Attached(media_slots::Kind::hardDisk, slot));
            EXPECT_EQ(*media.Path(media_slots::Kind::hardDisk, slot), hard);
            EXPECT_EQ(media.RequestedReadOnly(media_slots::Kind::hardDisk, slot), readOnly);
            EXPECT_EQ(settings.cartridges[slot], cartridge);
            EXPECT_EQ(settings.hardDisks[slot], hard);
            ASSERT_TRUE(roms.Eject(1 - slot));
            EXPECT_TRUE(roms.Inserted(slot));
            ASSERT_TRUE(media.Eject(media_slots::Kind::hardDisk, slot));
            ASSERT_TRUE(roms.Eject(slot));
            EXPECT_FALSE(roms.Inserted(slot));
        }
}

TEST_F(MediaSlotsTest, ExplicitReadOnlyMountsCanShareButWritableAliasesCannot)
{
    CMotherboard board;
    board.Reset();
    media_slots::Slots media(board);
    using media_slots::Kind;
    ASSERT_TRUE(media.Attach(Kind::floppy, 0, floppy, true));
    EXPECT_TRUE(media.ReadOnly(Kind::floppy, 0));
    EXPECT_TRUE(media.RequestedReadOnly(Kind::floppy, 0));
    ASSERT_TRUE(media.Attach(Kind::floppy, 1, directory / "." / floppy.filename(), true));
    EXPECT_TRUE(media.ReadOnly(Kind::floppy, 1));
    auto writable = media.Attach(Kind::floppy, 2, floppy);
    ASSERT_FALSE(writable);
    EXPECT_EQ(writable.error(), media_slots::Error::already_mounted);
    ASSERT_TRUE(media.Eject(Kind::floppy, 1));
    ASSERT_TRUE(media.Eject(Kind::floppy, 0));
    EXPECT_FALSE(media.RequestedReadOnly(Kind::floppy, 0));
    ASSERT_TRUE(media.Attach(Kind::floppy, 0, floppy));
    const auto shared = media.Attach(Kind::floppy, 1, floppy, true);
    ASSERT_FALSE(shared);
    EXPECT_EQ(shared.error(), media_slots::Error::already_mounted);
    const std::array<std::uint8_t, 24 * 1024> cartridge{};
    board.LoadROMCartridge(1, cartridge);
    ASSERT_TRUE(media.Attach(Kind::hardDisk, 0, hard, true));
    EXPECT_TRUE(media.ReadOnly(Kind::hardDisk, 0));
    EXPECT_TRUE(media.RequestedReadOnly(Kind::hardDisk, 0));
}

TEST_F(MediaSlotsTest, ExplicitHardDiskTakesPriorityOverConflictingRememberedFloppy)
{
    CMotherboard board;
    preview_roms::Roms roms(board);
    media_slots::Slots media(board);
    preview_settings::Settings settings;
    settings.restoreMedia = true;
    settings.floppies[0] = hard;
    preview_options::Options options;
    options.hardDisks[0] = hard;
    const auto errors = preview_media_restore::Restore(settings, options, roms, media);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_EQ(errors[0], "Floppy 1");
    EXPECT_FALSE(media.Attached(media_slots::Kind::floppy, 0));
}

TEST_F(MediaSlotsTest, ExplicitTapeTakesPriorityOverRememberedDiskAndRom)
{
    CMotherboard board;
    preview_roms::Roms roms(board);
    media_slots::Slots media(board);
    preview_settings::Settings settings;
    settings.restoreMedia = true;
    settings.systemRom = hard;
    settings.floppies[0] = hard;
    preview_options::Options options;
    options.tape = hard;
    const auto errors = preview_media_restore::Restore(settings, options, roms, media);
    ASSERT_EQ(errors.size(), 2u);
    EXPECT_EQ(errors[0], "System ROM");
    EXPECT_EQ(errors[1], "Floppy 1");
    EXPECT_FALSE(media.Attached(media_slots::Kind::floppy, 0));
}

TEST_F(MediaSlotsTest, RestoreRefusesToMountTheSettingsFile)
{
    CMotherboard board;
    preview_roms::Roms roms(board);
    media_slots::Slots media(board);
    preview_settings::Settings settings;
    settings.restoreMedia = true;
    settings.floppies[0] = floppy;
    const auto errors = preview_media_restore::Restore(settings, {}, roms, media, directory / "." / floppy.filename());
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_EQ(errors[0], "Floppy 1");
    EXPECT_FALSE(media.Attached(media_slots::Kind::floppy, 0));
}

TEST_F(MediaSlotsTest, RejectsInvalidOrOccupiedSlotsWithoutDetachingExistingDisk)
{
    CMotherboard board;
    board.Reset();
    media_slots::Slots media(board);
    using Kind = media_slots::Kind;
    const auto missingCartridge = media.Attach(Kind::hardDisk, 0, hard);
    ASSERT_FALSE(missingCartridge);
    EXPECT_EQ(missingCartridge.error(), media_slots::Error::missing_cartridge);
    EXPECT_FALSE(board.IsHardImageAttached(1));
    const std::array<std::uint8_t, 24 * 1024> cartridge{};
    board.LoadROMCartridge(1, cartridge);
    board.LoadROMCartridge(2, cartridge);
    for (const auto [kind, slot] : {std::pair{Kind::floppy, -1}, std::pair{Kind::floppy, 4},
                                   std::pair{Kind::hardDisk, -1}, std::pair{Kind::hardDisk, 2}})
    {
        const auto result = media.Attach(kind, slot, hard);
        ASSERT_FALSE(result);
        EXPECT_EQ(result.error(), media_slots::Error::invalid_slot);
        EXPECT_FALSE(media.Attached(kind, slot));
        EXPECT_FALSE(media.Eject(kind, slot));
    }
    for (const auto kind : {Kind::floppy, Kind::hardDisk})
    {
        const auto empty = media.Attach(kind, 0, {});
        ASSERT_FALSE(empty); EXPECT_EQ(empty.error(), media_slots::Error::empty_path);
        const auto missing = media.Attach(kind, 0, directory / "missing.img");
        ASSERT_FALSE(missing); EXPECT_EQ(missing.error(), media_slots::Error::attach_failed);
        const auto& path = kind == Kind::floppy ? floppy : hard;
        ASSERT_TRUE(media.Attach(kind, 0, path));
        EXPECT_TRUE(media.Conflicts(path));
        EXPECT_TRUE(media.Conflicts(path.parent_path() / "." / path.filename()));
        EXPECT_FALSE(media.Conflicts(directory / "missing.img"));
        const auto occupied = media.Attach(kind, 0, directory / "missing.img");
        ASSERT_FALSE(occupied); EXPECT_EQ(occupied.error(), media_slots::Error::occupied);
        ASSERT_TRUE(media.Attached(kind, 0));
        EXPECT_EQ(*media.Path(kind, 0), path);
        ASSERT_TRUE(media.Eject(kind, 0));
        EXPECT_FALSE(media.Conflicts(path));
    }
}

TEST_F(MediaSlotsTest, DetectsHardLinkToMountedDisk)
{
    const auto alias = directory / "alias.img";
    std::error_code error;
    std::filesystem::create_hard_link(hard, alias, error);
    if (error) GTEST_SKIP() << "Hard links unavailable: " << error.message();
    CMotherboard board;
    board.Reset();
    const std::array<std::uint8_t, 24 * 1024> cartridge{};
    board.LoadROMCartridge(1, cartridge);
    media_slots::Slots media(board);
    ASSERT_TRUE(media.Attach(media_slots::Kind::hardDisk, 0, hard));
    EXPECT_TRUE(media.Conflicts(alias));
    const auto shared = media.Attach(media_slots::Kind::floppy, 0, alias, true);
    ASSERT_FALSE(shared);
    EXPECT_EQ(shared.error(), media_slots::Error::already_mounted);
    ASSERT_TRUE(media.Eject(media_slots::Kind::hardDisk, 0));
    EXPECT_FALSE(media.Conflicts(alias));
}
}
