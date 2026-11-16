/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "EmbeddedRom.h"
#include "PreviewRoms.h"
#include "PreviewSnapshot.h"
#include "emubase/Emubase.h"
#include <gtest/gtest.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

void DebugLog(const wchar_t*) {}

namespace
{
class PreviewRomsTest : public testing::Test
{
protected:
    std::filesystem::path directory, system, cartridge, shortFile, snapshot;
    std::unique_ptr<CMotherboard> board;
    void SetUp() override
    {
        CProcessor::Init();
        board = std::make_unique<CMotherboard>();
        directory = std::filesystem::temp_directory_path() /
            ("ukncbtl-roms-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        ASSERT_TRUE(std::filesystem::create_directory(directory));
        system = directory / std::filesystem::path(u8"\u65e5\u672c-system.bin");
        cartridge = directory / std::filesystem::path(u8"\u65e5\u672c-cart.bin");
        shortFile = directory / "short.bin";
        snapshot = directory / "state.uknc";
        Write(system, embedded_rom::Size + 10, 0x42);
        Write(cartridge, 24 * 1024 + 10, 0xa5);
        Write(shortFile, 20, 0x31);
    }
    void Write(const std::filesystem::path& path, std::size_t count, std::uint8_t value)
    {
        const std::vector<std::uint8_t> data(count, value);
        std::ofstream output(path, std::ios::binary);
        ASSERT_TRUE(output.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size())));
    }
    void TearDown() override
    {
        board.reset();
        CProcessor::Done();
        std::error_code error;
        for (const auto& path : {system, cartridge, shortFile, snapshot}) std::filesystem::remove(path, error);
        std::filesystem::remove(directory / "alias.bin", error);
        std::filesystem::remove(directory, error);
    }
};

TEST_F(PreviewRomsTest, SystemRomLoadsTransactionallyAndCanReturnToEmbedded)
{
    preview_roms::Roms roms(*board);
    EXPECT_EQ(roms.Source(), preview_roms::SystemSource::embedded);
    EXPECT_EQ(board->GetROMByte(0), embedded_rom::Bytes().front());
    EXPECT_EQ(board->GetROMByte(32767), 0);
    ASSERT_TRUE(roms.LoadSystem(system));
    EXPECT_EQ(roms.Source(), preview_roms::SystemSource::file);
    ASSERT_NE(roms.SystemPath(), nullptr);
    EXPECT_EQ(*roms.SystemPath(), system);
    EXPECT_EQ(board->GetROMByte(0), 0x42);
    EXPECT_EQ(board->GetROMByte(static_cast<std::uint16_t>(embedded_rom::Size - 1)), 0x42);
    EXPECT_EQ(board->GetROMByte(32767), 0);
    const auto invalid = roms.LoadSystem(shortFile);
    ASSERT_FALSE(invalid); EXPECT_EQ(invalid.error(), preview_roms::Error::read_failed);
    EXPECT_EQ(board->GetROMByte(0), 0x42);
    EXPECT_EQ(*roms.SystemPath(), system);
    EXPECT_TRUE(roms.Conflicts(system.parent_path() / "." / system.filename()));
    roms.UseEmbedded();
    EXPECT_EQ(roms.Source(), preview_roms::SystemSource::embedded);
    EXPECT_EQ(roms.SystemPath(), nullptr);
    EXPECT_EQ(board->GetROMByte(0), embedded_rom::Bytes().front());
    EXPECT_FALSE(roms.Conflicts(system));
}

TEST_F(PreviewRomsTest, InsertsBothCartridgesAndPreservesOccupiedSlots)
{
    preview_roms::Roms roms(*board);
    for (int slot = 0; slot < 2; ++slot)
    {
        ASSERT_TRUE(roms.Insert(slot, cartridge));
        EXPECT_TRUE(board->IsROMCartridgeLoaded(slot + 1));
        EXPECT_EQ(board->GetROMCartByte(slot + 1, 0), 0xa5);
        EXPECT_EQ(board->GetROMCartByte(slot + 1, 24 * 1024 - 1), 0xa5);
        EXPECT_TRUE(roms.Conflicts(cartridge));
        const auto occupied = roms.Insert(slot, shortFile);
        ASSERT_FALSE(occupied); EXPECT_EQ(occupied.error(), preview_roms::Error::occupied);
        EXPECT_EQ(*roms.CartridgePath(slot), cartridge);
        ASSERT_TRUE(roms.Eject(slot));
        EXPECT_FALSE(roms.Inserted(slot));
        const auto shortResult = roms.Insert(slot, shortFile);
        ASSERT_FALSE(shortResult); EXPECT_EQ(shortResult.error(), preview_roms::Error::read_failed);
        EXPECT_FALSE(roms.Inserted(slot));
    }
    EXPECT_FALSE(roms.Insert(-1, cartridge));
    EXPECT_FALSE(roms.Insert(2, cartridge));
    EXPECT_FALSE(roms.Eject(2));
    EXPECT_FALSE(roms.Insert(0, {}));
    EXPECT_FALSE(roms.Conflicts(cartridge));
}

TEST_F(PreviewRomsTest, SnapshotRestorationChangesSystemSourceButKeepsCartridges)
{
    preview_roms::Roms roms(*board);
    board->Reset();
    ASSERT_TRUE(roms.LoadSystem(system));
    ASSERT_TRUE(roms.Insert(0, cartridge));
    preview_snapshot::Session session(*board);
    ASSERT_TRUE(session.Save(snapshot));
    roms.UseEmbedded();
    ASSERT_TRUE(session.Load(snapshot, [] {}));
    roms.MarkRestoredFromSnapshot();
    EXPECT_EQ(roms.Source(), preview_roms::SystemSource::snapshot);
    EXPECT_EQ(roms.SystemPath(), nullptr);
    EXPECT_EQ(board->GetROMByte(0), 0x42);
    EXPECT_TRUE(roms.Inserted(0));
    EXPECT_EQ(*roms.CartridgePath(0), cartridge);
}

TEST_F(PreviewRomsTest, DetectsHardLinksToLoadedRomSources)
{
    const auto alias = directory / "alias.bin";
    std::error_code error;
    std::filesystem::create_hard_link(cartridge, alias, error);
    if (error) GTEST_SKIP() << "Hard links unavailable: " << error.message();
    preview_roms::Roms roms(*board);
    ASSERT_TRUE(roms.Insert(1, cartridge));
    EXPECT_TRUE(roms.Conflicts(alias));
    ASSERT_TRUE(roms.Eject(1));
    EXPECT_FALSE(roms.Conflicts(alias));
}
}
