/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include <gtest/gtest.h>
#include "TestHelpers.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <vector>


namespace {
class CoreSmokeTests : public testing::Test
{
protected:
    void SetUp() override { CProcessor::Init(); }
    void TearDown() override { CProcessor::Done(); }
};

TEST_F(CoreSmokeTests, DisassemblesThreeWordInstruction)
{
    const std::array<uint16_t, 3> instruction{0012761, 1, 2};
    const auto decoded = DisassembleInstruction(instruction, 01000);
    ASSERT_TRUE(decoded);
    EXPECT_EQ(decoded->length, 3);
    EXPECT_EQ(decoded->mnemonic, L"MOV");
    EXPECT_EQ(decoded->argument, L"#000001, 000002(R1)");
}

TEST_F(CoreSmokeTests, DisassemblerRejectsEmptyAndPadsTruncatedSpans)
{
    EXPECT_FALSE(DisassembleInstruction({}, 01000));
    const std::array<uint16_t, 1> instruction{0012761};
    const auto decoded = DisassembleInstruction(instruction, 01000);
    ASSERT_TRUE(decoded);
    EXPECT_EQ(decoded->length, 3);
    EXPECT_EQ(decoded->argument, L"#000000, 000000(R1)");
}

TEST_F(CoreSmokeTests, JumpRequiresOperandAndRecognizesSelfTarget)
{
    const std::array<uint16_t, 1> incomplete{0000137};
    EXPECT_FALSE(Disasm_CheckForJump(incomplete, 01000));
    const std::array<uint16_t, 2> complete{0000137, 01000};
    EXPECT_EQ(Disasm_CheckForJump(complete, 01000), 0);
}

TEST_F(CoreSmokeTests, LastRamWordAlignsAndStoresLittleEndianBytes)
{
    CMotherboard board;
    board.Reset();
    board.SetRAMWord(0, 0xffff, 0x1234);
    EXPECT_EQ(board.GetRAMWord(0, 0xfffe), 0x1234);
    EXPECT_EQ(board.GetRAMByte(0, 0xfffe), 0x34);
    EXPECT_EQ(board.GetRAMByte(0, 0xffff), 0x12);
}

TEST_F(CoreSmokeTests, UnalignedSnapshotPreservesHeaderGuardsAndRoundTrips)
{
    CMotherboard board;
    board.Reset();
    board.SetRAMWord(0, 0xffff, 0x1234);
    board.GetCPU()->SetReg(0, 0x1234);
    board.GetPPU()->SetReg(1, 0xabcd);
    board.GetCPUMemoryController()->SetWord(0176640, false, 0123);
    std::vector<uint8_t> snapshot(UKNCIMAGE_SIZE + 2, 0xcc);
    board.SaveToImage(std::span<uint8_t, UKNCIMAGE_SIZE>{snapshot.data() + 1, UKNCIMAGE_SIZE});
    EXPECT_EQ(snapshot.front(), 0xcc);
    EXPECT_EQ(snapshot.back(), 0xcc);
    EXPECT_EQ(snapshot[1 + 160 + 2], 0x34);
    EXPECT_EQ(snapshot[1 + 160 + 3], 0x12);
    EXPECT_EQ(snapshot[1 + 224 + 4], 0xcd);
    EXPECT_EQ(snapshot[1 + 224 + 5], 0xab);
    EXPECT_EQ(snapshot[1 + 288], 0123);
    EXPECT_EQ(snapshot[1 + 289], 0);
    for (size_t i = 1; i <= 32; ++i) EXPECT_EQ(snapshot[i], 0xcc) << i;
    CMotherboard restored;
    restored.Reset();
    restored.LoadFromImage(std::span<const uint8_t, UKNCIMAGE_SIZE>{snapshot.data() + 1, UKNCIMAGE_SIZE});
    EXPECT_EQ(restored.GetCPU()->GetReg(0), 0x1234);
    EXPECT_EQ(restored.GetPPU()->GetReg(1), 0xabcd);
    EXPECT_EQ(restored.GetCPUMemoryController()->GetWord(0176640, false, false), 0123);
    EXPECT_EQ(restored.GetRAMWord(0, 0xfffe), 0x1234);
    std::vector<uint8_t> savedAgain(UKNCIMAGE_SIZE + 2, 0xcc);
    restored.SaveToImage(std::span<uint8_t, UKNCIMAGE_SIZE>{savedAgain.data() + 1, UKNCIMAGE_SIZE});
    EXPECT_EQ(savedAgain, snapshot);
}

TEST_F(CoreSmokeTests, FailedHardDriveAttachLeavesSlotEmpty)
{
    CMotherboard board;
    board.Reset();
    EXPECT_FALSE(board.AttachHardImage(1, std::filesystem::path{}));
    EXPECT_FALSE(board.IsHardImageAttached(1));
}

TEST_F(CoreSmokeTests, BusDeviceListsAreCompleteAndNonNull)
{
    CMotherboard board;
    board.Reset();
    const auto cpu = board.GetCPUBusDevices();
    const auto ppu = board.GetPPUBusDevices();
    EXPECT_EQ(cpu.size(), 5u);
    EXPECT_EQ(ppu.size(), 7u);
    for (const auto* device : cpu) EXPECT_NE(device, nullptr);
    for (const auto* device : ppu) EXPECT_NE(device, nullptr);
}

TEST_F(CoreSmokeTests, DebugTicksCanRunAfterReset)
{
    CMotherboard board;
    board.Reset();
    EXPECT_NO_FATAL_FAILURE(board.DebugTicks());
}

class CoreDiskSmokeTests : public CoreSmokeTests
{
protected:
    TempFile imageFile{L".hdi"};
    void SetUp() override
    {
        CoreSmokeTests::SetUp();
        std::vector<uint8_t> image(4 * 2 * 8 * 512);
        image[0] = 8;
        image[1] = 2;
        imageFile.Write(image);
    }
};

TEST_F(CoreDiskSmokeTests, HardDriveReadsSectorAndRejectsSectorZero)
{
    CHardDrive drive;
    ASSERT_TRUE(drive.AttachImage(imageFile.Path()));
    drive.Reset();
    drive.WritePort(0x1f2, 1);
    drive.WritePort(0x1f3, 1);
    drive.WritePort(0x1f7, 0x20);
    for (int i = 0; i < 5000 && (drive.ReadPort(0x1f7) & 0x80); ++i) drive.Periodic();
    ASSERT_EQ(drive.ReadPort(0x1f7) & 0x09, 0x08);
    EXPECT_EQ(drive.ReadPort(0x1f0), static_cast<uint16_t>(~0x0208u));
    for (int i = 1; i < 256; ++i) drive.ReadPort(0x1f0);
    drive.WritePort(0x1f2, 1);
    drive.WritePort(0x1f3, 0);
    drive.WritePort(0x1f7, 0x20);
    for (int i = 0; i < 5000 && (drive.ReadPort(0x1f7) & 0x80); ++i) drive.Periodic();
    const auto status = drive.ReadPort(0x1f7);
    EXPECT_NE(status & 0x01, 0);
    EXPECT_EQ(status & 0x08, 0);
}

TEST_F(CoreDiskSmokeTests, ReadOnlyFloppyCanDetachAfterWriteAttempt)
{
    imageFile.SetReadOnly(true);
    CFloppyController floppy;
    ASSERT_TRUE(floppy.AttachImage(0, imageFile.Path()));
    ASSERT_TRUE(floppy.IsReadOnly(0));
    floppy.SetCommand(FLOPPY_CMD_ENGINESTART | 02000 | 3);
    for (int i = 0; i < 47; ++i) floppy.Periodic();
    floppy.WriteData(0x1234);
    floppy.Periodic();
    floppy.DetachImage(0);
    EXPECT_FALSE(floppy.IsAttached(0));
    EXPECT_NE(floppy.GetState() & FLOPPY_STATUS_WRITEPROTECT, 0);
}
}
