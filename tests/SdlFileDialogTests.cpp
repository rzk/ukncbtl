/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "SdlFileDialog.h"
#include <gtest/gtest.h>
#include <memory>
#include <thread>

TEST(SdlFileDialog, DropBatchCopiesOnePathAndRejectsMultipleFiles)
{
    sdl_file_dialog::DropBatch batch;
    std::string path = "disk-日本.dsk";
    batch.Begin();
    batch.Add(path.c_str());
    path.clear();
    EXPECT_EQ(batch.Finish(), "disk-日本.dsk");
    EXPECT_FALSE(batch.Rejected());
    batch.Begin();
    batch.Add("first.dsk");
    batch.Add("second.dsk");
    EXPECT_FALSE(batch.Finish());
    EXPECT_TRUE(batch.Rejected());
    batch.Begin();
    batch.Add(nullptr);
    EXPECT_TRUE(batch.Rejected());
    EXPECT_FALSE(batch.Finish());
    batch.Begin();
    EXPECT_FALSE(batch.Finish());
    EXPECT_FALSE(batch.Rejected());
}

TEST(SdlFileDialog, HandsWorkerThreadResultToMainThread)
{
    auto state = std::make_shared<sdl_file_dialog::State>();
    ASSERT_TRUE(state->Begin());
    EXPECT_TRUE(state->Pending());
    EXPECT_FALSE(state->Begin());
    std::thread worker([state] {
        state->Complete({media_slots::Kind::hardDisk, 1, sdl_file_dialog::Status::selected, "disk.img"});
    });
    worker.join();
    EXPECT_FALSE(state->Pending());
    EXPECT_FALSE(state->Begin()); // An unread result cannot be overwritten.
    const auto result = state->Take();
    ASSERT_TRUE(result);
    EXPECT_EQ(result->kind, media_slots::Kind::hardDisk);
    EXPECT_EQ(result->slot, 1);
    EXPECT_EQ(result->status, sdl_file_dialog::Status::selected);
    EXPECT_EQ(result->path, "disk.img");
    EXPECT_FALSE(state->Take());
    EXPECT_TRUE(state->Begin());
    state->Complete({media_slots::Kind::floppy, 0, sdl_file_dialog::Status::canceled, {}});
    ASSERT_TRUE(state->Take());
}

TEST(SdlFileDialog, PendingCallbackCanOutliveOwner)
{
    auto owner = std::make_shared<sdl_file_dialog::State>();
    std::weak_ptr<sdl_file_dialog::State> weak = owner;
    ASSERT_TRUE(owner->Begin());
    auto callbackState = owner;
    owner.reset();
    ASSERT_FALSE(weak.expired());
    callbackState->Complete({media_slots::Kind::floppy, 3, sdl_file_dialog::Status::failed, {}});
    callbackState.reset();
    EXPECT_TRUE(weak.expired());
}

TEST(SdlFileDialog, PreservesScreenshotFilterAcrossCallback)
{
    sdl_file_dialog::State state;
    ASSERT_TRUE(state.Begin());
    state.Complete({media_slots::Kind::floppy, -1, sdl_file_dialog::Status::selected,
        "image", sdl_file_dialog::Purpose::screenshot, 1});
    const auto result = state.Take();
    ASSERT_TRUE(result);
    EXPECT_EQ(result->purpose, sdl_file_dialog::Purpose::screenshot);
    EXPECT_EQ(result->filter, 1);
    EXPECT_EQ(result->path, "image");
}

TEST(SdlFileDialog, PreservesSnapshotPurposeAcrossCallback)
{
    sdl_file_dialog::State state;
    for (const auto purpose : {sdl_file_dialog::Purpose::snapshotLoad, sdl_file_dialog::Purpose::snapshotSave})
    {
        ASSERT_TRUE(state.Begin());
        state.Complete({media_slots::Kind::floppy, -1, sdl_file_dialog::Status::selected, "state.uknc", purpose, -1});
        const auto result = state.Take();
        ASSERT_TRUE(result);
        EXPECT_EQ(result->purpose, purpose);
        EXPECT_EQ(result->path, "state.uknc");
    }
}

TEST(SdlFileDialog, PreservesRomPurposeAndCartridgeSlot)
{
    sdl_file_dialog::State state;
    for (const auto purpose : {sdl_file_dialog::Purpose::systemRom, sdl_file_dialog::Purpose::cartridge})
    {
        ASSERT_TRUE(state.Begin());
        state.Complete({media_slots::Kind::floppy, 1, sdl_file_dialog::Status::selected, "rom.bin", purpose, -1});
        const auto result = state.Take();
        ASSERT_TRUE(result);
        EXPECT_EQ(result->purpose, purpose);
        EXPECT_EQ(result->slot, 1);
        EXPECT_EQ(result->path, "rom.bin");
    }
}

TEST(SdlFileDialog, PreservesDiskGeometryAndPrinterPurpose)
{
    sdl_file_dialog::State state;
    for (int tracks : {40, 80})
    {
        ASSERT_TRUE(state.Begin());
        state.Complete({media_slots::Kind::floppy, tracks, sdl_file_dialog::Status::selected,
            "disk.dsk", sdl_file_dialog::Purpose::createDisk});
        const auto result = state.Take();
        ASSERT_TRUE(result);
        EXPECT_EQ(result->purpose, sdl_file_dialog::Purpose::createDisk);
        EXPECT_EQ(result->slot, tracks);
    }
    ASSERT_TRUE(state.Begin());
    state.Complete({media_slots::Kind::floppy, -1, sdl_file_dialog::Status::selected,
        "printer.log", sdl_file_dialog::Purpose::printer});
    EXPECT_EQ(state.Take()->purpose, sdl_file_dialog::Purpose::printer);
}

TEST(SdlFileDialog, PreservesReadOnlyMountChoiceAcrossCallback)
{
    sdl_file_dialog::State state;
    ASSERT_TRUE(state.Begin());
    state.Complete({media_slots::Kind::floppy, 2, sdl_file_dialog::Status::selected,
        "disk.dsk", sdl_file_dialog::Purpose::disk, -1, 0, 0, true});
    const auto result = state.Take();
    ASSERT_TRUE(result);
    EXPECT_TRUE(result->readOnly);
    EXPECT_EQ(result->slot, 2);
}

TEST(SdlFileDialog, PreservesTapePurpose)
{
    sdl_file_dialog::State state;
    for (const auto purpose : {sdl_file_dialog::Purpose::tapeOpen, sdl_file_dialog::Purpose::tapeCreate})
    {
        ASSERT_TRUE(state.Begin());
        state.Complete({media_slots::Kind::floppy, -1, sdl_file_dialog::Status::selected, "tape.wav", purpose});
        const auto result = state.Take();
        ASSERT_TRUE(result);
        EXPECT_EQ(result->purpose, purpose);
    }
}

TEST(SdlFileDialog, PreservesSubtitlePurpose)
{
    sdl_file_dialog::State state;
    ASSERT_TRUE(state.Begin());
    state.Complete({media_slots::Kind::floppy, -1, sdl_file_dialog::Status::selected,
        "listing.lst", sdl_file_dialog::Purpose::subtitles});
    const auto result = state.Take();
    ASSERT_TRUE(result);
    EXPECT_EQ(result->purpose, sdl_file_dialog::Purpose::subtitles);
    EXPECT_EQ(result->path, "listing.lst");
}

TEST(SdlFileDialog, PreservesLegacySettingsImportChoice)
{
    sdl_file_dialog::State state;
    ASSERT_TRUE(state.Begin());
    state.Complete({media_slots::Kind::floppy, 4, sdl_file_dialog::Status::selected,
        "legacy.ini", sdl_file_dialog::Purpose::legacySettings});
    const auto result = state.Take();
    ASSERT_TRUE(result);
    EXPECT_EQ(result->purpose, sdl_file_dialog::Purpose::legacySettings);
    EXPECT_EQ(result->slot % 3, 1); // Windows-1251.
    EXPECT_GE(result->slot, 3); // Include media paths.
}

TEST(SdlFileDialog, PreservesMemoryDumpSelectionAcrossCallback)
{
    sdl_file_dialog::State state;
    ASSERT_TRUE(state.Begin());
    state.Complete({media_slots::Kind::floppy, static_cast<int>(preview_debugger::MemorySource::ram2), sdl_file_dialog::Status::selected,
        "memory.bin", sdl_file_dialog::Purpose::memoryDump, -1, 01001, 02000});
    const auto result = state.Take();
    ASSERT_TRUE(result);
    EXPECT_EQ(result->slot, static_cast<int>(preview_debugger::MemorySource::ram2));
    EXPECT_EQ(result->start, 01001);
    EXPECT_EQ(result->end, 02000);
    EXPECT_EQ(result->purpose, sdl_file_dialog::Purpose::memoryDump);
    ASSERT_TRUE(state.Begin());
    state.Complete({media_slots::Kind::floppy, -1, sdl_file_dialog::Status::selected,
        "display.txt", sdl_file_dialog::Purpose::displayListDump});
    EXPECT_EQ(state.Take()->purpose, sdl_file_dialog::Purpose::displayListDump);
}

TEST(SdlFileDialog, PreservesConsoleLogPurpose)
{
    sdl_file_dialog::State state;
    ASSERT_TRUE(state.Begin());
    state.Complete({media_slots::Kind::floppy, -1, sdl_file_dialog::Status::selected,
        "console.txt", sdl_file_dialog::Purpose::consoleLog});
    const auto result = state.Take();
    ASSERT_TRUE(result);
    EXPECT_EQ(result->purpose, sdl_file_dialog::Purpose::consoleLog);
}
