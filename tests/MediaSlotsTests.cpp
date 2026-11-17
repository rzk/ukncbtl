/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "MediaSlots.h"
#include "PreviewMediaRestore.h"
#include "PreviewRoms.h"
#include "emubase/Emubase.h"
#include <gtest/gtest.h>
#include <chrono>
#include <array>
#include <filesystem>
#include <fstream>
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
