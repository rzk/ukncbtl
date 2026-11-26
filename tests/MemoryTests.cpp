/*  This file is part of UKNCBTL.
    UKNCBTL is free software: you can redistribute it and/or modify it under the terms
of the GNU Lesser General Public License as published by the Free Software Foundation,
either version 3 of the License, or (at your option) any later version.
    UKNCBTL is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
See the GNU Lesser General Public License for more details.
    You should have received a copy of the GNU Lesser General Public License along with
UKNCBTL. If not, see <http://www.gnu.org/licenses/>. */

// MemoryTests.cpp : tests for the CPU and PPU memory controllers: address decoding,
// RAM planes, ROM, I/O ports and bus errors
//
// Expected values come from the memory maps in the comments of Memory.cpp and from the
// UKNC documentation, not from the emulator output.

#include <gtest/gtest.h>
#include <cwchar>
#include <iterator>
#include <vector>
#include "TestHelpers.h"

namespace
{

const uint16_t STACK = 0400;

// Take the pending bus error, if any, of the CPU
bool CpuBusError(TestBoard& tb)
{
    tb.CPU()->SetSP(STACK);
    return tb.CPU()->InterruptProcessing();
}

// Take the pending bus error, if any, of the PPU
bool PpuBusError(TestBoard& tb)
{
    tb.PPU()->SetSP(STACK);
    return tb.PPU()->InterruptProcessing();
}

int CpuAddrType(TestBoard& tb, uint16_t address, bool haltMode, bool exec, uint16_t* pOffset)
{
    return tb.CpuMem()->TranslateAddress(address, haltMode, exec, pOffset, true);
}

int PpuAddrType(TestBoard& tb, uint16_t address, bool exec, uint16_t* pOffset)
{
    return tb.PpuMem()->TranslateAddress(address, false, exec, pOffset, true);
}

}

// CPU memory map

TEST(MemoryTests, Memory_CpuRamIsSplitBetweenPlanes1And2)
{
    // A CPU word at the address A is the byte A/2 of the plane 1 (low) and plane 2 (high)
    TestBoard tb;
    tb.SetWord(0200, 0x1234);
    ASSERT_EQ(0x34, tb.Board()->GetRAMByte(1, 0100)) << L"plane 1";
    ASSERT_EQ(0x12, tb.Board()->GetRAMByte(2, 0100)) << L"plane 2";
    ASSERT_EQ(0x00, tb.Board()->GetRAMByte(0, 0100)) << L"plane 0 is not touched";
    ASSERT_EQ(0x00, tb.Board()->GetRAMByte(0, 0200)) << L"plane 0 is not touched at the same address";

    tb.SetByte(0203, 0xAB);  // odd address: the high byte of the word 0202
    ASSERT_EQ(0xAB, tb.Board()->GetRAMByte(2, 0101)) << L"odd byte goes to the plane 2";
    tb.SetByte(0202, 0xCD);
    ASSERT_EQ(0xCD, tb.Board()->GetRAMByte(1, 0101)) << L"even byte goes to the plane 1";
    ASSERT_EQ(0xABCD, tb.GetWord(0202)) << L"the word";
}

TEST(MemoryTests, Memory_CpuRamLastWord)
{
    TestBoard tb;
    tb.SetWord(0157776, 0xBEEF);
    ASSERT_EQ(0xBEEF, tb.GetWord(0157776)) << L"last RAM word in USER mode";
    ASSERT_EQ(0xEF, tb.Board()->GetRAMByte(1, 0157776 / 2)) << L"plane 1";
    ASSERT_EQ(0xBE, tb.Board()->GetRAMByte(2, 0157776 / 2)) << L"plane 2";
}

TEST(MemoryTests, Memory_DebuggerViewReturnsTheValueAndAddressType)
{
    TestBoard tb;
    tb.SetWord(0200, 0x1234);
    const auto ram = tb.CpuMem()->GetWordView(0200, false, false);
    ASSERT_EQ(0x1234, ram.value) << L"RAM value";
    ASSERT_EQ(ADDRTYPE_RAM12, ram.addressType) << L"RAM type";
    const auto io = tb.CpuMem()->GetWordView(0176560, false, false);
    ASSERT_EQ(0, io.value) << L"I/O is not read by the debugger view";
    ASSERT_EQ(ADDRTYPE_IO, io.addressType) << L"I/O type";
}

TEST(MemoryTests, Memory_CpuAddressTypes)
{
    TestBoard tb;
    uint16_t offset;

    ASSERT_EQ(ADDRTYPE_RAM12, CpuAddrType(tb, 0, false, false, &offset)) << L"0, USER";
    ASSERT_EQ(0, offset) << L"offset of 0";
    ASSERT_EQ(ADDRTYPE_RAM12, CpuAddrType(tb, 0157776, false, true, &offset)) << L"157776, USER, exec";
    ASSERT_EQ(0157776, offset) << L"offset of 157776";
    ASSERT_EQ(ADDRTYPE_IO, CpuAddrType(tb, 0160000, false, false, &offset)) << L"160000, USER";
    ASSERT_EQ(ADDRTYPE_IO, CpuAddrType(tb, 0177776, false, false, &offset)) << L"177776, USER";

    ASSERT_EQ(ADDRTYPE_RAM12, CpuAddrType(tb, 0157776, true, false, &offset)) << L"157776, HALT";
    ASSERT_EQ(ADDRTYPE_RAM12, CpuAddrType(tb, 0160000, true, false, &offset)) << L"160000, HALT";
    ASSERT_EQ(ADDRTYPE_RAM12, CpuAddrType(tb, 0177776, true, false, &offset)) << L"177776, HALT";
    ASSERT_EQ(0177776, offset) << L"offset of 177776, HALT";
}

TEST(MemoryTests, Memory_CpuHaltModeSeesRamAtTheTopOfTheAddressSpace)
{
    TestBoard tb;
    tb.Board()->GetCPUMemoryController()->SetWord(0170000, true, 0x5AA5);
    ASSERT_EQ(0xA5, tb.Board()->GetRAMByte(1, 0170000 / 2)) << L"plane 1";
    ASSERT_EQ(0x5A, tb.Board()->GetRAMByte(2, 0170000 / 2)) << L"plane 2";
    ASSERT_EQ(0x5AA5, tb.Board()->GetCPUMemoryController()->GetWord(0170000, true, false)) << L"read back";
    ASSERT_FALSE(CpuBusError(tb)) << L"no bus error in HALT mode";
}

TEST(MemoryTests, Memory_CpuBusErrorOnUnknownPort)
{
    TestBoard tb;
    ASSERT_FALSE(CpuBusError(tb)) << L"nothing pending at the start";
    tb.Board()->GetCPUMemoryController()->GetWord(0170000, false, false);
    ASSERT_TRUE(CpuBusError(tb)) << L"read of the address 170000 in USER mode";
}

TEST(MemoryTests, Memory_CpuBusErrorOnUnknownPortWrite)
{
    TestBoard tb;
    tb.Board()->GetCPUMemoryController()->SetWord(0176000, false, 0);
    ASSERT_TRUE(CpuBusError(tb)) << L"write to the address 176000 in USER mode";
}

TEST(MemoryTests, Memory_CpuNoBusErrorInRam)
{
    TestBoard tb;
    tb.SetWord(0200, 1);
    tb.Board()->GetCPUMemoryController()->GetWord(0200, false, false);
    ASSERT_FALSE(CpuBusError(tb));
}

// CPU ports

TEST(MemoryTests, Memory_CpuPlaneRegisters)
{
    // 176640: plane address, 176642: data of the planes 1 (low byte) and 2 (high byte)
    TestBoard tb;
    tb.Board()->SetRAMByte(1, 0100, 0x34);
    tb.Board()->SetRAMByte(2, 0100, 0x12);
    CMemoryController* pCtl = tb.Board()->GetCPUMemoryController();

    pCtl->SetWord(0176640, false, 0100);
    ASSERT_EQ(0100, pCtl->GetWord(0176640, false, false)) << L"address register";
    ASSERT_EQ(0x1234, pCtl->GetWord(0176642, false, false)) << L"data register";

    pCtl->SetWord(0176642, false, 0xABCD);
    ASSERT_EQ(0xCD, tb.Board()->GetRAMByte(1, 0100)) << L"plane 1 after the word write";
    ASSERT_EQ(0xAB, tb.Board()->GetRAMByte(2, 0100)) << L"plane 2 after the word write";

    pCtl->SetByte(0176642, false, 0x11);
    pCtl->SetByte(0176643, false, 0x22);
    ASSERT_EQ(0x11, tb.Board()->GetRAMByte(1, 0100)) << L"plane 1 after the byte write";
    ASSERT_EQ(0x22, tb.Board()->GetRAMByte(2, 0100)) << L"plane 2 after the byte write";
    ASSERT_FALSE(CpuBusError(tb)) << L"no bus error";
}

TEST(MemoryTests, Memory_CpuSerialReceiver)
{
    TestBoard tb;
    CMemoryController* pCtl = tb.Board()->GetCPUMemoryController();
    ASSERT_EQ(0, pCtl->GetWord(0176570, false, false)) << L"status at the start";

    bool irq = tb.CpuMem()->SerialInput(0x41);
    ASSERT_FALSE(irq) << L"no interrupt when disabled";
    ASSERT_EQ(0200, pCtl->GetWord(0176570, false, false)) << L"ready";
    ASSERT_EQ(0x41, pCtl->GetWord(0176572, false, false)) << L"data";
    ASSERT_EQ(0, pCtl->GetWord(0176570, false, false)) << L"reading the data resets ready";
}

TEST(MemoryTests, Memory_CpuSerialReceiverOverflowAndInterrupt)
{
    TestBoard tb;
    CMemoryController* pCtl = tb.Board()->GetCPUMemoryController();

    tb.CpuMem()->SerialInput(0x41);
    tb.CpuMem()->SerialInput(0x42);  // the first byte was not read
    ASSERT_EQ(0010200, pCtl->GetWord(0176570, false, false) & 0010200) << L"ready and overflow";
    ASSERT_EQ(0x41, pCtl->GetWord(0176572, false, false)) << L"the first byte is kept";

    pCtl->SetWord(0176570, false, 0100);  // interrupt enable
    ASSERT_TRUE(tb.CpuMem()->SerialInput(0x43)) << L"interrupt is requested";
}

TEST(MemoryTests, Memory_CpuSerialTransmitter)
{
    TestBoard tb;
    CMemoryController* pCtl = tb.Board()->GetCPUMemoryController();
    ASSERT_EQ(0200, pCtl->GetWord(0176574, false, false)) << L"ready at the start";
    pCtl->SetWord(0176576, false, 'A');
    ASSERT_EQ(0, pCtl->GetWord(0176574, false, false) & 0200) << L"a byte is written: not ready";
}

TEST(MemoryTests, Memory_CpuNetworkReceiver)
{
    TestBoard tb;
    CMemoryController* pCtl = tb.Board()->GetCPUMemoryController();
    ASSERT_EQ(0200, pCtl->GetWord(0176564, false, false)) << L"transmitter is ready at the start";

    tb.CpuMem()->NetworkInput(0x77);
    ASSERT_EQ(0200, pCtl->GetWord(0176560, false, false)) << L"ready";
    ASSERT_EQ(0x77, pCtl->GetWord(0176562, false, false)) << L"data";
    ASSERT_EQ(0, pCtl->GetWord(0176560, false, false)) << L"reading the data resets ready";
}

TEST(MemoryTests, Memory_CpuNetworkStationNumberIsAddedToTheStatus)
{
    TestBoard tb;
    tb.Board()->SetNetStation(5);
    ASSERT_EQ(5, tb.Board()->GetCPUMemoryController()->GetWord(0176560, false, false) & 0377) << L"receiver status";
    ASSERT_EQ(0200 + 5, tb.Board()->GetCPUMemoryController()->GetWord(0176564, false, false) & 0377) << L"transmitter status";
}

TEST(MemoryTests, Memory_CpuChannelStatusOddBytes)
{
    static const uint16_t addresses[] = { 0177561, 0177565, 0176661, 0176665, 0176675 };
    for (size_t i = 0; i < sizeof(addresses) / sizeof(addresses[0]); i++)
    {
        TestBoard tb;
        tb.Board()->GetCPUMemoryController()->GetByte(addresses[i], false);
        wchar_t msg[40];
        std::swprintf(msg, std::size(msg), L"read of the byte %06o", (unsigned int) addresses[i]);
        ASSERT_FALSE(CpuBusError(tb)) << msg;
    }
}

// PPU memory map

TEST(MemoryTests, Memory_PpuRamIsPlane0)
{
    TestBoard tb;
    tb.SetPpuWord(0200, 0x1234);
    ASSERT_EQ(0x1234, tb.Board()->GetRAMWord(0, 0200)) << L"plane 0";
    ASSERT_EQ(0x00, tb.Board()->GetRAMByte(1, 0200)) << L"plane 1 is not touched";
    ASSERT_EQ(0x00, tb.Board()->GetRAMByte(2, 0200)) << L"plane 2 is not touched";
    ASSERT_EQ(0x1234, tb.GetPpuWord(0200)) << L"read back";
}

TEST(MemoryTests, Memory_PpuAddressTypesAtReset)
{
    // The window ports select the ROM at 100000-176777 after the reset
    TestBoard tb;
    uint16_t offset;

    ASSERT_EQ(ADDRTYPE_RAM0, PpuAddrType(tb, 0, false, &offset)) << L"0";
    ASSERT_EQ(ADDRTYPE_RAM0, PpuAddrType(tb, 077776, false, &offset)) << L"77776";
    ASSERT_EQ(077776, offset) << L"offset of 77776";
    ASSERT_EQ(ADDRTYPE_ROM, PpuAddrType(tb, 0100000, false, &offset)) << L"100000";
    ASSERT_EQ(0, offset) << L"offset of 100000";
    ASSERT_EQ(ADDRTYPE_ROM, PpuAddrType(tb, 0120000, false, &offset)) << L"120000";
    ASSERT_EQ(020000, offset) << L"offset of 120000";
    ASSERT_EQ(ADDRTYPE_ROM, PpuAddrType(tb, 0176776, false, &offset)) << L"176776";
    ASSERT_EQ(076776, offset) << L"offset of 176776";
    ASSERT_EQ(ADDRTYPE_IO, PpuAddrType(tb, 0177000, false, &offset)) << L"177000";
    ASSERT_EQ(ADDRTYPE_IO, PpuAddrType(tb, 0177776, false, &offset)) << L"177776";
    ASSERT_EQ(ADDRTYPE_DENY, PpuAddrType(tb, 0177000, true, &offset)) << L"177000, exec is denied";
}

TEST(MemoryTests, Memory_PpuReadsRom)
{
    TestBoard tb;
    std::vector<uint8_t> rom(32768);
    for (size_t i = 0; i < rom.size(); i++)
        rom[i] = (uint8_t) (i * 7 + 3);
    tb.Board()->LoadROM(std::span<const uint8_t, 32768>{rom});

    ASSERT_EQ(rom[0] | (rom[1] << 8), tb.GetPpuWord(0100000)) << L"first ROM word";
    ASSERT_EQ(rom[0x1234] | (rom[0x1235] << 8), tb.GetPpuWord(0100000 + 0x1234)) << L"ROM word 1234";
    ASSERT_EQ(rom[076776] | (rom[076777] << 8), tb.GetPpuWord(0176776)) << L"last ROM word below the ports";
    ASSERT_EQ(rom[0] | (rom[1] << 8), tb.Board()->GetROMWord(0)) << L"GetROMWord";
}

TEST(MemoryTests, Memory_PpuRomIsReadOnly)
{
    TestBoard tb;
    std::vector<uint8_t> rom(32768, 0x5A);
    tb.Board()->LoadROM(std::span<const uint8_t, 32768>{rom});

    tb.SetPpuWord(0100000, 0x1234);

    ASSERT_EQ(0x5A5A, tb.GetPpuWord(0100000)) << L"the word is not changed";
    ASSERT_FALSE(PpuBusError(tb)) << L"write to ROM is not an error";
}

TEST(MemoryTests, Memory_PpuWindowsCanSelectRam)
{
    // Port 177054: bit 4 = RAM in the window 100000-117777, bits 5, 6, 7 = RAM in the next three windows
    TestBoard tb;
    uint16_t offset;
    tb.SetPpuWord(0177054, 0360);

    ASSERT_EQ(ADDRTYPE_RAM0, PpuAddrType(tb, 0100000, false, &offset)) << L"window 0";
    ASSERT_EQ(0100000, offset) << L"offset in window 0";
    ASSERT_EQ(ADDRTYPE_RAM0, PpuAddrType(tb, 0120000, false, &offset)) << L"window 1";
    ASSERT_EQ(ADDRTYPE_RAM0, PpuAddrType(tb, 0140000, false, &offset)) << L"window 2";
    ASSERT_EQ(ADDRTYPE_RAM0, PpuAddrType(tb, 0160000, false, &offset)) << L"window 3";
    ASSERT_EQ(0160000, offset) << L"offset in window 3";
    ASSERT_EQ(ADDRTYPE_IO, PpuAddrType(tb, 0177000, false, &offset)) << L"the ports stay";

    tb.SetPpuWord(0100000, 0xCAFE);
    tb.SetPpuWord(0176776, 0xBABE);
    ASSERT_EQ(0xCAFE, tb.Board()->GetRAMWord(0, 0100000)) << L"RAM at 100000";
    ASSERT_EQ(0xBABE, tb.Board()->GetRAMWord(0, 0176776)) << L"RAM at 176776";
}

TEST(MemoryTests, Memory_PpuWindowsAreSelectedOneByOne)
{
    TestBoard tb;
    uint16_t offset;
    tb.SetPpuWord(0177054, 0101);  // window 0 is the ROM (bit 0); only the window 2 = 140000-157777 is RAM (bit 6)

    ASSERT_EQ(ADDRTYPE_ROM, PpuAddrType(tb, 0100000, false, &offset)) << L"window 0";
    ASSERT_EQ(ADDRTYPE_ROM, PpuAddrType(tb, 0120000, false, &offset)) << L"window 1";
    ASSERT_EQ(ADDRTYPE_RAM0, PpuAddrType(tb, 0140000, false, &offset)) << L"window 2";
    ASSERT_EQ(ADDRTYPE_ROM, PpuAddrType(tb, 0160000, false, &offset)) << L"window 3";
}

TEST(MemoryTests, Memory_PpuRomCartridgeWindow)
{
    // Bits 1, 2 of the port 177054 select the bank 1..3 of the cartridge in the slot 1
    TestBoard tb;
    uint16_t offset;
    tb.SetPpuWord(0177054, 02);

    ASSERT_EQ(ADDRTYPE_DENY, PpuAddrType(tb, 0100000, false, &offset)) << L"no cartridge: access denied";

    std::vector<uint8_t> cart(24 * 1024);
    for (size_t i = 0; i < cart.size(); i++)
        cart[i] = (uint8_t) (i ^ (i >> 8));
    tb.Board()->LoadROMCartridge(1, std::span<const uint8_t, 24 * 1024>{cart});

    ASSERT_EQ(ADDRTYPE_ROMCART1, PpuAddrType(tb, 0100000, false, &offset)) << L"bank 1";
    ASSERT_EQ(0, offset) << L"bank 1 starts at the cartridge start";
    ASSERT_EQ(cart[0] | (cart[1] << 8), tb.GetPpuWord(0100000)) << L"bank 1 word";

    tb.SetPpuWord(0177054, 04);
    ASSERT_EQ(ADDRTYPE_ROMCART1, PpuAddrType(tb, 0100000, false, &offset)) << L"bank 2";
    ASSERT_EQ(020000, offset) << L"bank 2 is 8 KB further";
    ASSERT_EQ(cart[020000] | (cart[020001] << 8), tb.GetPpuWord(0100000)) << L"bank 2 word";

    tb.SetPpuWord(0177054, 06);
    ASSERT_EQ(ADDRTYPE_ROMCART1, PpuAddrType(tb, 0100000, false, &offset)) << L"bank 3";
    ASSERT_EQ(040000, offset) << L"bank 3 is 16 KB further";
}

TEST(MemoryTests, Memory_PpuRomCartridgeInSlot2)
{
    TestBoard tb;
    uint16_t offset;
    std::vector<uint8_t> cart(24 * 1024, 0x11);
    tb.Board()->LoadROMCartridge(2, std::span<const uint8_t, 24 * 1024>{cart});
    tb.SetPpuWord(0177054, 012);  // bit 3 = slot 2, bank 1

    ASSERT_EQ(ADDRTYPE_ROMCART2, PpuAddrType(tb, 0100000, false, &offset)) << L"slot 2";
    ASSERT_EQ(0x1111, tb.GetPpuWord(0100000)) << L"word";
}

// PPU ports

TEST(MemoryTests, Memory_PpuPlaneRegisters)
{
    // 177010: plane address, 177012: plane 0 data, 177014: planes 1 (low) and 2 (high) data
    TestBoard tb;
    CMemoryController* pCtl = tb.Board()->GetPPUMemoryController();
    tb.Board()->SetRAMByte(0, 0100, 0x55);
    tb.Board()->SetRAMByte(1, 0100, 0x34);
    tb.Board()->SetRAMByte(2, 0100, 0x12);

    pCtl->SetWord(0177010, false, 0100);
    ASSERT_EQ(0100, pCtl->GetWord(0177010, false, false)) << L"address register";
    ASSERT_EQ(0x55, pCtl->GetWord(0177012, false, false)) << L"plane 0 data";
    ASSERT_EQ(0x1234, pCtl->GetWord(0177014, false, false)) << L"planes 1 and 2 data";

    pCtl->SetWord(0177012, false, 0x00AA);
    pCtl->SetWord(0177014, false, 0xBBCC);
    ASSERT_EQ(0xAA, tb.Board()->GetRAMByte(0, 0100)) << L"plane 0";
    ASSERT_EQ(0xCC, tb.Board()->GetRAMByte(1, 0100)) << L"plane 1";
    ASSERT_EQ(0xBB, tb.Board()->GetRAMByte(2, 0100)) << L"plane 2";
    ASSERT_FALSE(PpuBusError(tb)) << L"no bus error";
}

TEST(MemoryTests, Memory_PpuBusErrorOnUnknownPort)
{
    TestBoard tb;
    ASSERT_FALSE(PpuBusError(tb)) << L"nothing pending at the start";
    tb.Board()->GetPPUMemoryController()->GetWord(0177040, false, false);
    ASSERT_TRUE(PpuBusError(tb)) << L"read of the port 177040";
}

TEST(MemoryTests, Memory_PpuBusErrorOnMouseWithoutMouse)
{
    TestBoard tb;
    tb.Board()->GetPPUMemoryController()->GetWord(0177400, false, false);
    ASSERT_TRUE(PpuBusError(tb)) << L"the mouse port is absent when the mouse is off";
}

TEST(MemoryTests, Memory_PpuMousePort)
{
    TestBoard tb;
    tb.PpuMem()->SetMouse(true);
    tb.PpuMem()->MouseMove(5, 0, true, false);
    uint16_t value = tb.Board()->GetPPUMemoryController()->GetWord(0177400, false, false);
    ASSERT_EQ(5, (value >> 1) & 0177) << L"dx";
    ASSERT_EQ(0x0100, value & 0x0101) << L"left button";
    ASSERT_FALSE(PpuBusError(tb)) << L"no bus error";
}

// Keyboard

TEST(MemoryTests, Memory_PpuKeyboardPressAndRelease)
{
    TestBoard tb;
    CMemoryController* pCtl = tb.Board()->GetPPUMemoryController();
    ASSERT_EQ(0, pCtl->GetWord(0177700, false, false)) << L"status at the start";

    tb.PpuMem()->KeyboardEvent(0x2A, true);
    ASSERT_EQ(0200, pCtl->GetWord(0177700, false, false) & 0200) << L"ready after a press";
    ASSERT_EQ(0x2A, pCtl->GetWord(0177702, false, false)) << L"pressed key code";
    ASSERT_EQ(0, pCtl->GetWord(0177700, false, false) & 0200) << L"reading the data resets ready";

    tb.PpuMem()->KeyboardEvent(0x2A, false);
    ASSERT_EQ(0200, pCtl->GetWord(0177700, false, false) & 0200) << L"ready after a release";
    ASSERT_EQ(0200 | 0x0A, pCtl->GetWord(0177702, false, false)) << L"released key: bit 7 and the low 4 bits";
}

TEST(MemoryTests, Memory_PpuKeyboardInterrupt)
{
    TestBoard tb;
    CMemoryController* pCtl = tb.Board()->GetPPUMemoryController();

    tb.PpuMem()->KeyboardEvent(0x10, true);
    ASSERT_EQ(0, tb.PPU()->GetVIRQ(3)) << L"no interrupt when disabled";
    pCtl->GetWord(0177702, false, false);

    pCtl->SetWord(0177700, false, 0100);  // interrupt enable
    tb.PpuMem()->KeyboardEvent(0x10, true);
    ASSERT_EQ(0300, tb.PPU()->GetVIRQ(3)) << L"interrupt vector 300";

    pCtl->GetWord(0177702, false, false);
    ASSERT_EQ(0, tb.PPU()->GetVIRQ(3)) << L"reading the data cancels the request";
}

// System control register

TEST(MemoryTests, Memory_PpuStartsAndStopsCpu)
{
    // 177716: bit 15 = ACLO released (the CPU starts), bit 5 = DCLO (the CPU stops), bit 4 = HALT
    TestBoard tb;
    ASSERT_TRUE(tb.CPU()->IsStopped()) << L"the CPU is stopped after the reset";

    tb.SetPpuWord(0177716, 0100000);
    ASSERT_FALSE(tb.CPU()->IsStopped()) << L"the CPU runs";

    tb.SetPpuWord(0177716, 040);
    ASSERT_TRUE(tb.CPU()->IsStopped()) << L"the CPU is stopped by DCLO";
}

// Bus devices

TEST(MemoryTests, Memory_BusDevicesHaveNamesAndRanges)
{
    TestBoard tb;
    const auto lists = std::array{tb.Board()->GetCPUBusDevices(), tb.Board()->GetPPUBusDevices()};
    for (const auto devices : lists)
    {
        ASSERT_FALSE(devices.empty()) << L"there are devices";
        for (const CBusDevice* pDevice : devices)
        {
            ASSERT_TRUE(pDevice != nullptr) << L"device exists";
            ASSERT_TRUE(pDevice->GetName() != nullptr && pDevice->GetName()[0] != 0) << L"device name";
            ASSERT_FALSE(pDevice->GetAddressRanges().empty()) << L"device has at least one range";
        }
    }
}

TEST(MemoryTests, Memory_BusDevicesDoNotOverlap)
{
    TestBoard tb;
    const auto lists = std::array{tb.Board()->GetCPUBusDevices(), tb.Board()->GetPPUBusDevices()};
    for (const auto devices : lists)
    {
        std::vector<int> owner(65536, -1);
        int deviceCount = 0;
        for (const CBusDevice* pDevice : devices)
        {
            for (const auto& range : pDevice->GetAddressRanges())
            {
                ASSERT_TRUE(range.length != 0 && static_cast<uint32_t>(range.start) + range.length <= owner.size());
                for (uint32_t a = range.start; a < static_cast<uint32_t>(range.start) + range.length; a++)
                {
                    wchar_t msg[80];
                    std::swprintf(msg, std::size(msg), L"address %06o of the device %ls", a, pDevice->GetName());
                    ASSERT_EQ(-1, owner[a]) << msg;
                    owner[a] = deviceCount;
                }
            }
            ++deviceCount;
        }
    }
}
