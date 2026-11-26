/*  This file is part of UKNCBTL.
    UKNCBTL is free software: you can redistribute it and/or modify it under the terms
of the GNU Lesser General Public License as published by the Free Software Foundation,
either version 3 of the License, or (at your option) any later version.
    UKNCBTL is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
See the GNU Lesser General Public License for more details.
    You should have received a copy of the GNU Lesser General Public License along with
UKNCBTL. If not, see <http://www.gnu.org/licenses/>. */

// BoardTests.cpp : tests for CMotherboard: creation, reset, memory loading, CPU-PPU channels,
// the timer, the 50 Hz event and the emulator image
//
// Expected values come from the UKNC documentation (vectors, port bits), not from the emulator
// output.

#include <gtest/gtest.h>
#include <cwchar>
#include <iterator>
#include <vector>
#include "TestHelpers.h"

namespace
{

const uint16_t STACK = 0400;
uint64_t tapeSampleCount = 0;

bool CountTapeSamples(unsigned int samples)
{
    tapeSampleCount += samples;
    return false;
}

// Run the timer for the given number of 2 microsecond ticks
void TimerTicks(CMotherboard* pBoard, int count)
{
    for (int i = 0; i < count; i++)
        pBoard->TimerTick();
}

}

// Creation and reset

TEST(BoardTests, Board_BreakpointsRespectSpanLength)
{
    TestBoard tb;
    auto& board = *tb.Board();
    board.GetCPU()->SetDCLOPin(true);
    board.GetPPU()->SetDCLOPin(true);
    board.GetCPU()->SetPC(01000);
    board.GetPPU()->SetPC(02000);
    const std::array<uint16_t, 2> cpuBreakpoints{03000, 01000};
    board.SetCPUBreakpoints(std::span{cpuBreakpoints}.first<1>());
    ASSERT_TRUE(board.SystemFrame()) << L"address beyond the span is not a breakpoint";
    board.SetCPUBreakpoints(cpuBreakpoints);
    ASSERT_FALSE(board.SystemFrame()) << L"the last span element is a CPU breakpoint";
    board.SetCPUBreakpoints({});
    const std::array<uint16_t, 1> ppuBreakpoints{02000};
    board.SetPPUBreakpoints(ppuBreakpoints);
    ASSERT_FALSE(board.SystemFrame()) << L"PPU breakpoints work without CPU breakpoints";
    board.SetPPUBreakpoints({});
    ASSERT_TRUE(board.SystemFrame()) << L"empty spans disable both breakpoint lists";
}

TEST(BoardTests, Board_CreateAndReset)
{
    CMotherboard board;
    board.Reset();

    ASSERT_NE(board.GetCPU(), nullptr);
    ASSERT_NE(board.GetPPU(), nullptr);
    ASSERT_NE(board.GetCPUMemoryController(), nullptr);
    ASSERT_NE(board.GetPPUMemoryController(), nullptr);
}

TEST(BoardTests, Board_RamWordRoundTrip)
{
    CMotherboard board;
    board.Reset();

    for (int plan = 0; plan < 3; plan++)
    {
        board.SetRAMWord(plan, 01000, 0123456);
        ASSERT_EQ(0123456u, (unsigned int) board.GetRAMWord(plan, 01000));
    }
}

TEST(BoardTests, Board_ResetStartsPpuButNotCpu)
{
    CMotherboard board;
    board.Reset();
    ASSERT_FALSE(board.GetPPU()->IsStopped()) << L"PPU runs";
    ASSERT_TRUE(board.GetCPU()->IsStopped()) << L"CPU waits for the PPU";
}

TEST(BoardTests, Board_RamIsCleared)
{
    CMotherboard board;
    for (int plan = 0; plan < 3; plan++)
    {
        ASSERT_EQ(0, board.GetRAMWord(plan, 0)) << L"first word";
        ASSERT_EQ(0, board.GetRAMWord(plan, 0xFFFE)) << L"last word";
    }
}

TEST(BoardTests, Board_LoadRamLoads32Kilobytes)
{
    std::vector<uint8_t> buffer(32768);
    for (size_t i = 0; i < buffer.size(); i++)
        buffer[i] = (uint8_t) (i * 5 + 1);
    for (int plan = 0; plan < 3; plan++)
    {
        CMotherboard board;
        board.LoadRAM(plan, std::span<const uint8_t, 32768>{buffer});
        ASSERT_EQ(buffer[0] | (buffer[1] << 8), board.GetRAMWord(plan, 0)) << L"first word";
        ASSERT_EQ(buffer[32766] | (buffer[32767] << 8), board.GetRAMWord(plan, 32766)) << L"last loaded word";
        ASSERT_EQ(0, board.GetRAMWord(plan, 32768)) << L"the second half is not loaded";
    }
}

TEST(BoardTests, Board_LoadRomAndCartridge)
{
    CMotherboard board;
    std::vector<uint8_t> rom(32768);
    for (size_t i = 0; i < rom.size(); i++)
        rom[i] = (uint8_t) (i + 9);
    board.LoadROM(std::span<const uint8_t, 32768>{rom});
    ASSERT_EQ(rom[0] | (rom[1] << 8), board.GetROMWord(0)) << L"ROM first word";
    ASSERT_EQ(rom[100] | (rom[101] << 8), board.GetROMWord(100)) << L"ROM word 100";
    ASSERT_EQ(rom[100], board.GetROMByte(100)) << L"ROM byte 100";
    ASSERT_EQ(rom[101], board.GetROMByte(101)) << L"ROM byte 101";

    ASSERT_FALSE(board.IsROMCartridgeLoaded(1)) << L"cartridge 1 is absent";
    ASSERT_EQ(0xFFFF, board.GetROMCartWord(1, 0)) << L"an absent cartridge reads as ones";
    std::vector<uint8_t> cart(24 * 1024, 0x6C);
    board.LoadROMCartridge(1, std::span<const uint8_t, 24 * 1024>{cart});
    ASSERT_TRUE(board.IsROMCartridgeLoaded(1)) << L"cartridge 1 is loaded";
    ASSERT_FALSE(board.IsROMCartridgeLoaded(2)) << L"cartridge 2 is still absent";
    ASSERT_EQ(0x6C6C, board.GetROMCartWord(1, 0)) << L"cartridge word";
    board.UnloadROMCartridge(1);
    ASSERT_FALSE(board.IsROMCartridgeLoaded(1)) << L"cartridge 1 is unloaded";
}

// CPU-PPU channels

TEST(BoardTests, Board_ChannelsAfterReset)
{
    TestBoard tb;
    tb.Board()->ChanResetByCPU();
    tb.Board()->ChanResetByPPU();

    for (int ch = 0; ch < 3; ch++)
        ASSERT_EQ(0200, tb.Board()->ChanTxStateGetCPU(ch)) << L"CPU transmitter is ready";
    for (int ch = 0; ch < 2; ch++)
        ASSERT_EQ(0, tb.Board()->ChanRxStateGetCPU(ch)) << L"CPU receiver is empty";
    ASSERT_EQ(0, tb.Board()->ChanRxStateGetPPU()) << L"PPU receivers are empty";
    ASSERT_EQ(030, tb.Board()->ChanTxStateGetPPU()) << L"PPU transmitters 0 and 1 are ready";
}

TEST(BoardTests, Board_ChannelsCpuToPpu)
{
    for (uint8_t ch = 0; ch < 3; ch++)
    {
        TestBoard tb;
        CMotherboard* pBoard = tb.Board();
        pBoard->ChanResetByCPU();
        pBoard->ChanResetByPPU();

        pBoard->ChanWriteByCPU(ch, 0xA0 + ch);
        ASSERT_EQ(0, pBoard->ChanTxStateGetCPU(ch)) << L"CPU transmitter is busy";
        ASSERT_EQ(1 << (3 + ch), pBoard->ChanRxStateGetPPU() & (1 << (3 + ch))) << L"PPU receiver has data";

        ASSERT_EQ(0xA0 + ch, pBoard->ChanReadByPPU(ch)) << L"data";
        ASSERT_EQ(0, pBoard->ChanRxStateGetPPU() & (1 << (3 + ch))) << L"PPU receiver is empty";
        ASSERT_EQ(0200, pBoard->ChanTxStateGetCPU(ch)) << L"CPU transmitter is ready again";
    }
}

TEST(BoardTests, Board_ChannelsPpuToCpu)
{
    for (uint8_t ch = 0; ch < 2; ch++)
    {
        TestBoard tb;
        CMotherboard* pBoard = tb.Board();
        pBoard->ChanResetByCPU();
        pBoard->ChanResetByPPU();

        pBoard->ChanWriteByPPU(ch, 0xB0 + ch);
        ASSERT_EQ(0, pBoard->ChanTxStateGetPPU() & (1 << (3 + ch))) << L"PPU transmitter is busy";
        ASSERT_EQ(0200, pBoard->ChanRxStateGetCPU(ch)) << L"CPU receiver has data";

        ASSERT_EQ(0xB0 + ch, pBoard->ChanReadByCPU(ch)) << L"data";
        ASSERT_EQ(0, pBoard->ChanRxStateGetCPU(ch)) << L"CPU receiver is empty";
        ASSERT_EQ(1 << (3 + ch), pBoard->ChanTxStateGetPPU() & (1 << (3 + ch))) << L"PPU transmitter is ready again";
    }
}

TEST(BoardTests, Board_ChannelsThroughThePorts)
{
    // The CPU writes the channel 0 data register 177566, the PPU reads it from 177060
    TestBoard tb;
    tb.Board()->ChanResetByCPU();
    tb.Board()->ChanResetByPPU();

    tb.Board()->GetCPUMemoryController()->SetByte(0177566, false, 0x42);
    ASSERT_EQ(0x42, tb.Board()->GetPPUMemoryController()->GetByte(0177060, false)) << L"PPU reads the byte";

    // The PPU writes the channel 1 data register 177072, the CPU reads it from 176662
    tb.Board()->GetPPUMemoryController()->SetByte(0177072, false, 0x43);
    ASSERT_EQ(0x43, tb.Board()->GetCPUMemoryController()->GetByte(0176662, false)) << L"CPU reads the byte";
}

TEST(BoardTests, Board_ChannelCpuReceiverInterrupts)
{
    // Vectors: channel 0 receiver 060, channel 1 receiver 460
    TestBoard tb;
    tb.StartCPU();
    tb.Board()->ChanResetByCPU();
    tb.Board()->ChanResetByPPU();

    tb.Board()->ChanRxStateSetCPU(0, 0100);  // interrupt enable
    tb.Board()->ChanRxStateSetCPU(1, 0100);
    tb.Board()->ChanWriteByPPU(0, 1);
    ASSERT_EQ(060, tb.CPU()->GetVIRQ(1)) << L"channel 0 vector";
    tb.Board()->ChanWriteByPPU(1, 2);
    ASSERT_EQ(0460, tb.CPU()->GetVIRQ(3)) << L"channel 1 vector";

    tb.Board()->ChanReadByCPU(0);
    ASSERT_EQ(0, tb.CPU()->GetVIRQ(1)) << L"reading the data cancels the request, channel 0";
    tb.Board()->ChanReadByCPU(1);
    ASSERT_EQ(0, tb.CPU()->GetVIRQ(3)) << L"reading the data cancels the request, channel 1";
}

TEST(BoardTests, Board_ChannelCpuTransmitterInterrupts)
{
    // Vectors: channel 0 transmitter 064, channel 1 transmitter 464, channel 2 transmitter 474.
    // The transmitters are ready after the reset, so enabling the interrupt requests it at once.
    TestBoard tb;
    tb.StartCPU();
    tb.Board()->ChanResetByCPU();
    tb.Board()->ChanResetByPPU();

    tb.Board()->ChanTxStateSetCPU(0, 0100);
    ASSERT_EQ(064, tb.CPU()->GetVIRQ(2)) << L"channel 0 vector";
    tb.Board()->ChanTxStateSetCPU(1, 0100);
    ASSERT_EQ(0464, tb.CPU()->GetVIRQ(4)) << L"channel 1 vector";
    tb.Board()->ChanTxStateSetCPU(2, 0100);
    ASSERT_EQ(0474, tb.CPU()->GetVIRQ(5)) << L"channel 2 vector";

    tb.Board()->ChanTxStateSetCPU(0, 0);
    ASSERT_EQ(0, tb.CPU()->GetVIRQ(2)) << L"disabling the interrupt cancels the request";
}

TEST(BoardTests, Board_ChannelPpuReceiverInterrupts)
{
    // Vectors: channel 0 receiver 320, channel 1 receiver 330, channel 2 receiver 340
    TestBoard tb;
    tb.Board()->ChanResetByCPU();
    tb.Board()->ChanResetByPPU();

    tb.Board()->ChanRxStateSetPPU(07);  // interrupt enable for the three channels
    tb.Board()->ChanWriteByCPU(0, 1);
    ASSERT_EQ(0320, tb.PPU()->GetVIRQ(5)) << L"channel 0 vector";
    tb.Board()->ChanWriteByCPU(1, 2);
    ASSERT_EQ(0330, tb.PPU()->GetVIRQ(7)) << L"channel 1 vector";
    tb.Board()->ChanWriteByCPU(2, 3);
    ASSERT_EQ(0340, tb.PPU()->GetVIRQ(9)) << L"channel 2 vector";
}

TEST(BoardTests, Board_ChannelPpuTransmitterInterrupts)
{
    // Vectors: channel 0 transmitter 324, channel 1 transmitter 334
    TestBoard tb;
    tb.Board()->ChanResetByCPU();
    tb.Board()->ChanResetByPPU();

    tb.Board()->ChanTxStateSetPPU(03);
    ASSERT_EQ(0324, tb.PPU()->GetVIRQ(6)) << L"channel 0 vector";
    ASSERT_EQ(0334, tb.PPU()->GetVIRQ(8)) << L"channel 1 vector";
}

TEST(BoardTests, Board_ChannelInterruptIsTakenByTheCpu)
{
    TestBoard tb;
    tb.StartCPU();
    tb.Board()->ChanResetByCPU();
    tb.Board()->ChanResetByPPU();
    tb.SetWord(060, 02000);  // handler address
    tb.SetWord(062, 0);      // handler PSW
    tb.Board()->ChanRxStateSetCPU(0, 0100);
    tb.Board()->ChanWriteByPPU(0, 1);
    tb.CPU()->SetSP(STACK);

    ASSERT_TRUE(tb.CPU()->InterruptProcessing()) << L"interrupt is taken";

    ASSERT_EQ(02000, tb.CPU()->GetPC()) << L"PC";
    ASSERT_EQ(STACK - 4, tb.CPU()->GetSP()) << L"SP";
    ASSERT_EQ(0, tb.CPU()->GetVIRQ(1)) << L"the request is gone";
}

// Timer

TEST(BoardTests, Board_TimerCountsDown)
{
    TestBoard tb;
    CMotherboard* pBoard = tb.Board();
    pBoard->SetTimerReload(5);
    ASSERT_EQ(5, pBoard->GetTimerValueView()) << L"a stopped timer holds the reload value";

    TimerTicks(pBoard, 3);
    ASSERT_EQ(5, pBoard->GetTimerValueView()) << L"a stopped timer does not count";

    pBoard->SetTimerState(1);  // start
    TimerTicks(pBoard, 2);
    ASSERT_EQ(3, pBoard->GetTimerValueView()) << L"2 ticks after the start";
}

TEST(BoardTests, Board_TimerReloadsAtZero)
{
    TestBoard tb;
    CMotherboard* pBoard = tb.Board();
    pBoard->SetTimerReload(3);
    pBoard->SetTimerState(1);

    TimerTicks(pBoard, 2);
    ASSERT_EQ(1, pBoard->GetTimerValueView()) << L"2 ticks";
    ASSERT_EQ(0, pBoard->GetTimerStateView() & 0200) << L"no zero flag yet";

    TimerTicks(pBoard, 1);
    ASSERT_EQ(3, pBoard->GetTimerValueView()) << L"reloaded after 0";
    ASSERT_EQ(0200, pBoard->GetTimerStateView() & 0200) << L"zero flag";
}

TEST(BoardTests, Board_TimerPrescaler)
{
    // Bits 2, 1 of the state: 00 = 2 us, 01 = 4 us, 10 = 8 us, 11 = 16 us per count; the tick is 2 us
    static const struct { uint16_t state; int ticksPerCount; } cases[] =
    {
        { 01, 1 }, { 03, 2 }, { 05, 4 }, { 07, 8 },
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
    {
        TestBoard tb;
        CMotherboard* pBoard = tb.Board();
        pBoard->SetTimerReload(100);
        pBoard->SetTimerState(cases[i].state);

        TimerTicks(pBoard, cases[i].ticksPerCount * 3);

        wchar_t msg[40];
        std::swprintf(msg, std::size(msg), L"state %03o", (unsigned int) cases[i].state);
        ASSERT_EQ(97, pBoard->GetTimerValueView()) << msg;
    }
}

TEST(BoardTests, Board_TimerOverflowFlag)
{
    // The zero flag is set at 0; reaching 0 again before the flag is read sets the overflow flag too
    TestBoard tb;
    CMotherboard* pBoard = tb.Board();
    pBoard->SetTimerReload(2);
    pBoard->SetTimerState(1);

    TimerTicks(pBoard, 4);

    ASSERT_EQ(0211, pBoard->GetTimerState()) << L"running, overflow, zero";
    ASSERT_EQ(0201, pBoard->GetTimerState()) << L"reading the state clears the overflow flag";
}

TEST(BoardTests, Board_TimerReloadIs12Bit)
{
    TestBoard tb;
    tb.Board()->SetTimerReload(0177777);
    ASSERT_EQ(07777, tb.Board()->GetTimerReload()) << L"reload";
    ASSERT_EQ(07777, tb.Board()->GetTimerValueView()) << L"value";
}

TEST(BoardTests, Board_TimerInterrupt)
{
    // The PPU interrupt vector of the timer is 304
    TestBoard tb;
    tb.Board()->SetTimerReload(1);
    tb.Board()->SetTimerState(0101);  // run, interrupt enable
    ASSERT_EQ(0, tb.PPU()->GetVIRQ(2)) << L"no request before";

    TimerTicks(tb.Board(), 1);

    ASSERT_EQ(0304, tb.PPU()->GetVIRQ(2)) << L"vector";
}

TEST(BoardTests, Board_TimerNoInterruptWhenDisabled)
{
    TestBoard tb;
    tb.Board()->SetTimerReload(1);
    tb.Board()->SetTimerState(1);
    TimerTicks(tb.Board(), 3);
    ASSERT_EQ(0, tb.PPU()->GetVIRQ(2)) << L"no request";
}

TEST(BoardTests, Board_TimerPorts)
{
    // The PPU ports: 177710 state, 177712 reload, 177714 counter
    TestBoard tb;
    CMemoryController* pCtl = tb.Board()->GetPPUMemoryController();
    pCtl->SetWord(0177712, false, 4);
    pCtl->SetWord(0177710, false, 1);
    TimerTicks(tb.Board(), 3);

    ASSERT_EQ(1, pCtl->GetWord(0177714, false, false)) << L"counter";
    ASSERT_EQ(1, pCtl->GetWord(0177710, false, false) & 1) << L"state: running";
}

// 50 Hz event

TEST(BoardTests, Board_Tick50InterruptsCpuAndPpu)
{
    // The event vector is 100. The bits 8 and 9 of the port 177054 mask the PPU and CPU events,
    // they are set after the power on.
    TestBoard tb;
    tb.StartCPU();
    tb.SetPpuWord(0177054, 01);  // unmask both
    tb.SetWord(0100, 02000);
    tb.SetWord(0102, 0);
    tb.SetPpuWord(0100, 03000);
    tb.SetPpuWord(0102, 0);

    tb.Board()->Tick50();
    tb.CPU()->SetSP(STACK);
    tb.PPU()->SetSP(STACK);

    ASSERT_TRUE(tb.CPU()->InterruptProcessing()) << L"CPU takes the event";
    ASSERT_EQ(02000, tb.CPU()->GetPC()) << L"CPU PC";
    ASSERT_TRUE(tb.PPU()->InterruptProcessing()) << L"PPU takes the event";
    ASSERT_EQ(03000, tb.PPU()->GetPC()) << L"PPU PC";
}

TEST(BoardTests, Board_Tick50IsMaskedAfterPowerOn)
{
    TestBoard tb;
    tb.StartCPU();
    tb.Board()->Tick50();
    tb.CPU()->SetSP(STACK);
    tb.PPU()->SetSP(STACK);
    ASSERT_FALSE(tb.CPU()->InterruptProcessing()) << L"CPU event is masked";
    ASSERT_FALSE(tb.PPU()->InterruptProcessing()) << L"PPU event is masked";
}

TEST(BoardTests, Board_Tick50IsIgnoredWhenInterruptsAreDisabled)
{
    // Bit 7 of the PSW (priority) blocks the event
    TestBoard tb;
    tb.StartCPU();
    tb.SetPpuWord(0177054, 01);
    tb.CPU()->SetPSW(0200);
    tb.Board()->Tick50();
    tb.CPU()->SetSP(STACK);
    ASSERT_FALSE(tb.CPU()->InterruptProcessing());
}

TEST(BoardTests, Board_TapeSamplingPreservesFractionalRates)
{
    TestBoard tb;
    for (const int sampleRate : {1, 44101})
    {
        tapeSampleCount = 0;
        tb.Board()->SetTapeReadCallback(CountTapeSamples, sampleRate);
        for (int frame = 0; frame < FRAMERATE; ++frame)
            ASSERT_TRUE(tb.Board()->SystemFrame());
        ASSERT_EQ(static_cast<uint64_t>(sampleRate), tapeSampleCount);
    }
    tb.Board()->SetTapeReadCallback(nullptr, 0);
}

TEST(BoardTests, Board_TapeSamplingBatchesHighRates)
{
    TestBoard tb;
    tapeSampleCount = 0;
    tb.Board()->SetTapeReadCallback(CountTapeSamples, 1000000);
    ASSERT_TRUE(tb.Board()->SystemFrame());
    ASSERT_EQ(static_cast<uint64_t>(20000), tapeSampleCount);
    tb.Board()->SetTapeReadCallback(nullptr, 0);
}

// Emulator image

TEST(BoardTests, Board_ImageSize)
{
    // 512 bytes of the header, 32 KB of ROM, three 64 KB RAM planes
    ASSERT_EQ(512 + 32 * 1024 + 3 * 64 * 1024, UKNCIMAGE_SIZE) << L"image size";
    ASSERT_EQ(512, UKNCIMAGE_HEADER_SIZE) << L"header size";
}

TEST(BoardTests, Board_SaveAndLoadImage)
{
    TestBoard a;
    CMotherboard* pa = a.Board();

    // Memory
    std::vector<uint8_t> rom(32768);
    for (size_t i = 0; i < rom.size(); i++)
        rom[i] = (uint8_t) (i * 3 + 7);
    pa->LoadROM(std::span<const uint8_t, 32768>{rom});
    pa->SetRAMByte(0, 0x1234, 0x11);
    pa->SetRAMByte(1, 0x2345, 0x22);
    pa->SetRAMByte(2, 0xFFFF, 0x33);
    // Processors
    a.CPU()->SetReg(0, 0x1111);
    a.CPU()->SetReg(5, 0x5555);
    a.CPU()->SetSP(0x2222);
    a.CPU()->SetPSW(012);
    a.PPU()->SetReg(1, 0x7777);
    // Timer
    pa->SetTimerReload(0123);
    pa->SetTimerState(0103);
    TimerTicks(pa, 6);
    // Channels
    pa->ChanResetByCPU();
    pa->ChanResetByPPU();
    pa->ChanWriteByCPU(1, 0x5A);
    // Ports
    a.CpuMem()->SetWord(0176640, false, 0123);
    a.CpuMem()->SerialInput(0x41);
    a.SetPpuWord(0177010, 0456);
    a.SetPpuWord(0177054, 0360);
    a.PpuMem()->KeyboardEvent(0x2A, true);

    std::vector<uint8_t> image(UKNCIMAGE_SIZE, 0);
    pa->SaveToImage(std::span<uint8_t, UKNCIMAGE_SIZE>{image});

    TestBoard b;
    CMotherboard* pb = b.Board();
    pb->LoadFromImage(std::span<const uint8_t, UKNCIMAGE_SIZE>{image});

    ASSERT_EQ(rom[0] | (rom[1] << 8), pb->GetROMWord(0)) << L"ROM";
    ASSERT_EQ(rom[32766] | (rom[32767] << 8), pb->GetROMWord(32766)) << L"ROM end";
    ASSERT_EQ(0x11, pb->GetRAMByte(0, 0x1234)) << L"RAM plane 0";
    ASSERT_EQ(0x22, pb->GetRAMByte(1, 0x2345)) << L"RAM plane 1";
    ASSERT_EQ(0x33, pb->GetRAMByte(2, 0xFFFF)) << L"RAM plane 2";

    ASSERT_EQ(0x1111, b.CPU()->GetReg(0)) << L"CPU R0";
    ASSERT_EQ(0x5555, b.CPU()->GetReg(5)) << L"CPU R5";
    ASSERT_EQ(0x2222, b.CPU()->GetSP()) << L"CPU SP";
    ASSERT_EQ(012, b.CPU()->GetPSW()) << L"CPU PSW";
    ASSERT_EQ(0x7777, b.PPU()->GetReg(1)) << L"PPU R1";

    ASSERT_EQ(pa->GetTimerValueView(), pb->GetTimerValueView()) << L"timer value";
    ASSERT_EQ(pa->GetTimerReloadView(), pb->GetTimerReloadView()) << L"timer reload";
    ASSERT_EQ(pa->GetTimerStateView(), pb->GetTimerStateView()) << L"timer state";

    ASSERT_EQ(0x5A, pb->ChanReadByPPU(1)) << L"channel data";

    ASSERT_EQ(0123, b.Board()->GetCPUMemoryController()->GetWord(0176640, false, false)) << L"CPU port 176640";
    ASSERT_EQ(0200, b.Board()->GetCPUMemoryController()->GetWord(0176570, false, false)) << L"serial receiver is ready";
    ASSERT_EQ(0x41, b.Board()->GetCPUMemoryController()->GetWord(0176572, false, false)) << L"serial data";
    ASSERT_EQ(0456, b.Board()->GetPPUMemoryController()->GetWord(0177010, false, false)) << L"PPU port 177010";
    ASSERT_EQ(0360, b.Board()->GetPPUMemoryController()->GetWord(0177054, false, false)) << L"PPU port 177054";
    ASSERT_EQ(0200, b.Board()->GetPPUMemoryController()->GetWord(0177700, false, false) & 0200) << L"keyboard is ready";
    ASSERT_EQ(0x2A, b.Board()->GetPPUMemoryController()->GetWord(0177702, false, false)) << L"key code";
}

TEST(BoardTests, Board_LoadImageRestoresTheMemoryMap)
{
    TestBoard a;
    a.SetPpuWord(0177054, 0360);  // RAM in all the windows
    std::vector<uint8_t> image(UKNCIMAGE_SIZE, 0);
    a.Board()->SaveToImage(std::span<uint8_t, UKNCIMAGE_SIZE>{image});

    TestBoard b;
    uint16_t offset;
    ASSERT_EQ(ADDRTYPE_ROM, b.PpuMem()->TranslateAddress(0100000, false, false, &offset, true)) << L"ROM before the load";
    b.Board()->LoadFromImage(std::span<const uint8_t, UKNCIMAGE_SIZE>{image});
    ASSERT_EQ(ADDRTYPE_RAM0, b.PpuMem()->TranslateAddress(0100000, false, false, &offset, true)) << L"RAM after the load";
}

TEST(BoardTests, Board_ImageKeepsTheHeaderUntouched)
{
    // The board writes only its own part of the header; the first 32 bytes belong to the application
    TestBoard a;
    std::vector<uint8_t> image(UKNCIMAGE_SIZE, 0xEE);
    a.Board()->SaveToImage(std::span<uint8_t, UKNCIMAGE_SIZE>{image});
    for (int i = 0; i < 32; i++)
        ASSERT_EQ(0xEE, image[i]) << L"header byte";
}
