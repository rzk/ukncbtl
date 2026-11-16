/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "MediaSlots.h"
#include "emubase/Emubase.h"
#include <gtest/gtest.h>
#include <chrono>
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
        ASSERT_TRUE(media.Attach(media_slots::Kind::hardDisk, slot, hard));
        EXPECT_TRUE(board.IsHardImageAttached(slot + 1));
        ASSERT_NE(media.Path(media_slots::Kind::hardDisk, slot), nullptr);
        EXPECT_EQ(*media.Path(media_slots::Kind::hardDisk, slot), hard);
        EXPECT_EQ(media.ReadOnly(media_slots::Kind::hardDisk, slot), board.IsHardImageReadOnly(slot + 1));
        ASSERT_TRUE(media.Eject(media_slots::Kind::hardDisk, slot));
        EXPECT_FALSE(board.IsHardImageAttached(slot + 1));
    }
}

TEST_F(MediaSlotsTest, RejectsInvalidOrOccupiedSlotsWithoutDetachingExistingDisk)
{
    CMotherboard board;
    board.Reset();
    media_slots::Slots media(board);
    using Kind = media_slots::Kind;
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
    media_slots::Slots media(board);
    ASSERT_TRUE(media.Attach(media_slots::Kind::hardDisk, 0, hard));
    EXPECT_TRUE(media.Conflicts(alias));
    ASSERT_TRUE(media.Eject(media_slots::Kind::hardDisk, 0));
    EXPECT_FALSE(media.Conflicts(alias));
}
}
