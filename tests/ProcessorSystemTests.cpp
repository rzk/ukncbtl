/*  This file is part of UKNCBTL.
    UKNCBTL is free software: you can redistribute it and/or modify it under the terms
of the GNU Lesser General Public License as published by the Free Software Foundation,
either version 3 of the License, or (at your option) any later version.
    UKNCBTL is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
See the GNU Lesser General Public License for more details.
    You should have received a copy of the GNU Lesser General Public License along with
UKNCBTL. If not, see <http://www.gnu.org/licenses/>. */

// ProcessorSystemTests.cpp : T-bit trace trap, RTI / RTT, WAIT and RESET of the KM1801VM2 (CPU, USER mode)
//
// Expected values come from the PDP-11 / KM1801VM2 descriptions of the instructions, not from the
// emulator output:
//   - the T-bit (bit 4 of the PSW) causes the trap through the vector 014 after the instruction is executed,
//     the pushed PC is the address of the next instruction;
//   - RTI that restores the PSW with the T-bit set traps at once, before the next instruction;
//     RTT does not: the next instruction is executed first;
//   - WAIT stops the processor until an interrupt; the trace trap does not wake it up;
//   - RESET initializes the I/O devices and cancels the pending line clock request.

#include <gtest/gtest.h>
#include <cwchar>
#include <iterator>
#include "TestHelpers.h"

namespace
{

const uint16_t PROG = 01000;
const uint16_t HANDLER = 02000;
const uint16_t STACK = 0400;

const uint16_t TBIT = 020;         // the trace bit
const uint16_t PRIORITY_BIT = 0200; // interrupts of the lower priority are blocked

const uint16_t OP_INC_R0 = 0005200;
const uint16_t OP_RTI = 0000002;
const uint16_t OP_RTT = 0000006;
const uint16_t OP_WAIT = 0000001;
const uint16_t OP_RESET = 0000005;

// The program at PROG, SP = STACK, registers zero
void Prepare(TestBoard& tb, const uint16_t* program, size_t count)
{
    uint16_t regs[8] = { 0, 0, 0, 0, 0, 0, STACK, 0 };
    tb.SetRegs(regs);
    tb.LoadProgram(PROG, program, count);
}

// The trace trap vector 014 leads to the HANDLER with the PSW 0
void SetTraceVector(TestBoard& tb)
{
    tb.SetWord(014, HANDLER);
    tb.SetWord(016, 0);
}

}

// T-bit

TEST(ProcessorSystemTests, Processor_Tbit_TrapsAfterTheInstruction)
{
    TestBoard tb;
    const uint16_t program[] = { OP_INC_R0, OP_INC_R0 };
    Prepare(tb, program, 2);
    SetTraceVector(tb);
    tb.CPU()->SetPSW(TBIT);

    tb.Run();
    ASSERT_EQ(1, tb.CPU()->GetReg(0)) << L"the traced instruction is executed";
    ASSERT_TRUE(tb.TakeInterrupt()) << L"trace trap";

    ASSERT_EQ(HANDLER, tb.CPU()->GetPC()) << L"PC = handler";
    ASSERT_EQ(0, tb.CPU()->GetPSW()) << L"PSW from the vector: T-bit is cleared";
    ASSERT_EQ(STACK - 4, tb.CPU()->GetSP()) << L"SP";
    ASSERT_EQ(PROG + 2, tb.GetWord(STACK - 4)) << L"the pushed PC is the next instruction";
    ASSERT_EQ(TBIT, tb.GetWord(STACK - 2)) << L"the pushed PSW has the T-bit";
    ASSERT_EQ(1, tb.CPU()->GetReg(0)) << L"the next instruction is not executed yet";
}

TEST(ProcessorSystemTests, Processor_Tbit_NoTrapWithoutTheBit)
{
    TestBoard tb;
    const uint16_t program[] = { OP_INC_R0 };
    Prepare(tb, program, 1);
    SetTraceVector(tb);

    tb.Run();
    ASSERT_FALSE(tb.TakeInterrupt()) << L"no trap";
    ASSERT_EQ(PROG + 2, tb.CPU()->GetPC()) << L"PC";
}

TEST(ProcessorSystemTests, Processor_Tbit_TrapsAfterEveryInstruction)
{
    // The handler returns with RTT, which lets one instruction run: INC, trap, RTT, INC, trap, ...
    TestBoard tb;
    const uint16_t program[] = { OP_INC_R0, OP_INC_R0, OP_INC_R0 };
    Prepare(tb, program, 3);
    SetTraceVector(tb);
    tb.SetWord(HANDLER, OP_RTT);
    tb.CPU()->SetPSW(TBIT);

    for (int n = 1; n <= 3; n++)
    {
        tb.Run();  // the traced instruction
        ASSERT_EQ(n, tb.CPU()->GetReg(0)) << L"R0";
        ASSERT_TRUE(tb.TakeInterrupt()) << L"trace trap after the instruction";
        ASSERT_EQ(PROG + n * 2, tb.GetWord(STACK - 4)) << L"the pushed PC";
        ASSERT_EQ(HANDLER, tb.CPU()->GetPC()) << L"PC = handler";

        tb.Run();  // RTT
        ASSERT_EQ(PROG + n * 2, tb.CPU()->GetPC()) << L"back to the program";
        ASSERT_EQ(TBIT, tb.CPU()->GetPSW()) << L"the T-bit is restored";
        ASSERT_EQ(STACK, tb.CPU()->GetSP()) << L"SP";
        ASSERT_FALSE(tb.TakeInterrupt()) << L"RTT inhibits the trap before the next instruction";
    }
}

TEST(ProcessorSystemTests, Processor_Tbit_RtiTrapsBeforeTheNextInstruction)
{
    // RTI restores the PSW with the T-bit set and the trace trap occurs at once
    TestBoard tb;
    const uint16_t program[] = { OP_INC_R0, OP_INC_R0 };
    Prepare(tb, program, 2);
    SetTraceVector(tb);
    tb.SetWord(HANDLER, OP_RTI);
    tb.CPU()->SetSP(STACK - 4);
    tb.SetWord(STACK - 4, PROG);   // the return address
    tb.SetWord(STACK - 2, TBIT);  // the PSW to restore
    tb.CPU()->SetPC(HANDLER);

    tb.Run();  // RTI
    ASSERT_EQ(PROG, tb.CPU()->GetPC()) << L"PC after RTI";
    ASSERT_EQ(TBIT, tb.CPU()->GetPSW()) << L"the T-bit is restored";
    ASSERT_TRUE(tb.TakeInterrupt()) << L"trace trap before the instruction at PROG";

    ASSERT_EQ(0, tb.CPU()->GetReg(0)) << L"the instruction at PROG is not executed";
    ASSERT_EQ(PROG, tb.GetWord(STACK - 4)) << L"the pushed PC is the return address";
    ASSERT_EQ(HANDLER, tb.CPU()->GetPC()) << L"PC = handler";
}

TEST(ProcessorSystemTests, Processor_Tbit_RttRunsOneInstructionFirst)
{
    // RTT restores the same PSW, but the trap waits until the next instruction is executed
    TestBoard tb;
    const uint16_t program[] = { OP_INC_R0, OP_INC_R0 };
    Prepare(tb, program, 2);
    SetTraceVector(tb);
    tb.SetWord(HANDLER, OP_RTT);
    tb.CPU()->SetSP(STACK - 4);
    tb.SetWord(STACK - 4, PROG);
    tb.SetWord(STACK - 2, TBIT);
    tb.CPU()->SetPC(HANDLER);

    tb.Run();  // RTT
    ASSERT_EQ(PROG, tb.CPU()->GetPC()) << L"PC after RTT";
    ASSERT_EQ(TBIT, tb.CPU()->GetPSW()) << L"the T-bit is restored";
    ASSERT_FALSE(tb.TakeInterrupt()) << L"no trap right after RTT";

    tb.Run();  // the instruction at PROG
    ASSERT_EQ(1, tb.CPU()->GetReg(0)) << L"the instruction at PROG is executed";
    ASSERT_TRUE(tb.TakeInterrupt()) << L"trace trap after it";
    ASSERT_EQ(PROG + 2, tb.GetWord(STACK - 4)) << L"the pushed PC is the next instruction";
}

TEST(ProcessorSystemTests, Processor_Tbit_ReturnWithoutTheBitDoesNotTrap)
{
    // RTI and RTT with the PSW without the T-bit: no trap after them
    static const uint16_t opcodes[] = { OP_RTI, OP_RTT };
    for (size_t i = 0; i < 2; i++)
    {
        TestBoard tb;
        const uint16_t program[] = { OP_INC_R0 };
        Prepare(tb, program, 1);
        SetTraceVector(tb);
        tb.SetWord(HANDLER, opcodes[i]);
        tb.CPU()->SetSP(STACK - 4);
        tb.SetWord(STACK - 4, PROG);
        tb.SetWord(STACK - 2, 0);
        tb.CPU()->SetPC(HANDLER);

        tb.Run();
        ASSERT_EQ(PROG, tb.CPU()->GetPC()) << L"PC after the return";
        ASSERT_FALSE(tb.TakeInterrupt()) << L"no trap";
    }
}

TEST(ProcessorSystemTests, Processor_Tbit_FlagsAreKeptInThePushedPsw)
{
    TestBoard tb;
    const uint16_t program[] = { OP_INC_R0 };
    Prepare(tb, program, 1);
    SetTraceVector(tb);
    tb.CPU()->SetPSW(TBIT | TF_C);  // C is not changed by INC

    tb.Run();
    ASSERT_TRUE(tb.TakeInterrupt());
    ASSERT_EQ(TBIT | TF_C, tb.GetWord(STACK - 2)) << L"T and C in the pushed PSW";
}

TEST(ProcessorSystemTests, Processor_Tbit_TrapInstructionGoesThroughItsOwnVector)
{
    // EMT with the T-bit: the EMT trap has the priority, vector 030; the pushed PSW has the T-bit
    TestBoard tb;
    const uint16_t program[] = { 0104001 };  // EMT 1
    Prepare(tb, program, 1);
    SetTraceVector(tb);
    tb.SetWord(030, 03000);
    tb.SetWord(032, 0);
    tb.CPU()->SetPSW(TBIT);

    tb.Run();
    ASSERT_EQ(03000, tb.CPU()->GetPC()) << L"PC = EMT handler";
    ASSERT_EQ(TBIT, tb.GetWord(STACK - 2)) << L"the pushed PSW";
}

// WAIT

TEST(ProcessorSystemTests, Processor_Wait_StopsTheInstructionFlow)
{
    TestBoard tb;
    const uint16_t program[] = { OP_WAIT, OP_INC_R0 };
    Prepare(tb, program, 2);

    tb.Run();
    ASSERT_EQ(PROG + 2, tb.CPU()->GetPC()) << L"PC after WAIT";
    tb.Run(5);
    ASSERT_EQ(PROG + 2, tb.CPU()->GetPC()) << L"PC does not move";
    ASSERT_EQ(0, tb.CPU()->GetReg(0)) << L"the next instruction is not executed";
    ASSERT_FALSE(tb.TakeInterrupt()) << L"nothing wakes the processor up";
}

TEST(ProcessorSystemTests, Processor_Wait_InterruptWakesTheProcessorUp)
{
    TestBoard tb;
    tb.StartCPU();
    const uint16_t program[] = { OP_WAIT, OP_INC_R0 };
    Prepare(tb, program, 2);
    tb.SetWord(060, HANDLER);
    tb.SetWord(062, 0);
    tb.SetWord(HANDLER, OP_RTI);

    tb.Run();
    tb.Run(3);
    ASSERT_EQ(0, tb.CPU()->GetReg(0)) << L"waiting";

    tb.CPU()->InterruptVIRQ(1, 060);
    ASSERT_TRUE(tb.TakeInterrupt()) << L"the interrupt is taken";
    ASSERT_EQ(HANDLER, tb.CPU()->GetPC()) << L"PC = handler";
    ASSERT_EQ(PROG + 2, tb.GetWord(STACK - 4)) << L"the pushed PC is the address after WAIT";

    tb.Run();  // RTI of the handler
    ASSERT_EQ(PROG + 2, tb.CPU()->GetPC()) << L"back after WAIT";
    tb.Run();  // INC R0: the processor runs again
    ASSERT_EQ(1, tb.CPU()->GetReg(0)) << L"the instruction after WAIT is executed";
}

TEST(ProcessorSystemTests, Processor_Wait_BlockedInterruptDoesNotWakeTheProcessorUp)
{
    // The bit 7 of the PSW blocks the interrupts of the devices
    TestBoard tb;
    tb.StartCPU();
    const uint16_t program[] = { OP_WAIT, OP_INC_R0 };
    Prepare(tb, program, 2);
    tb.SetWord(060, HANDLER);
    tb.SetWord(062, 0);
    tb.CPU()->SetPSW(PRIORITY_BIT);

    tb.Run();
    tb.CPU()->InterruptVIRQ(1, 060);
    ASSERT_FALSE(tb.TakeInterrupt()) << L"the interrupt is blocked";
    tb.Run(3);
    ASSERT_EQ(PROG + 2, tb.CPU()->GetPC()) << L"still waiting";
    ASSERT_EQ(0, tb.CPU()->GetReg(0)) << L"the next instruction is not executed";
}

TEST(ProcessorSystemTests, Processor_Wait_TraceTrapDoesNotWakeTheProcessorUp)
{
    TestBoard tb;
    const uint16_t program[] = { OP_WAIT, OP_INC_R0 };
    Prepare(tb, program, 2);
    SetTraceVector(tb);
    tb.CPU()->SetPSW(TBIT);

    tb.Run();
    ASSERT_FALSE(tb.TakeInterrupt()) << L"no trace trap while waiting";
    tb.Run(3);
    ASSERT_FALSE(tb.TakeInterrupt()) << L"and after more cycles";
    ASSERT_EQ(0, tb.CPU()->GetReg(0)) << L"the next instruction is not executed";
}

// RESET

TEST(ProcessorSystemTests, Processor_Reset_TakesItsTime)
{
    // The KM1801VM2 technical description: RESET takes 105 + 968 ticks
    TestBoard tb;
    const uint16_t program[] = { OP_RESET };
    Prepare(tb, program, 1);
    tb.Run();
    ASSERT_EQ(PROG + 2, tb.CPU()->GetPC()) << L"PC";
    ASSERT_EQ(105 + 968, tb.CPU()->GetInternalTick()) << L"execution time";
}

TEST(ProcessorSystemTests, Processor_Reset_InitializesTheChannels)
{
    TestBoard tb;
    CMotherboard* pBoard = tb.Board();
    const uint16_t program[] = { OP_RESET };
    Prepare(tb, program, 1);

    // a byte for the PPU is in the channel, the CPU transmitter is busy
    pBoard->ChanResetByCPU();
    pBoard->ChanResetByPPU();
    pBoard->ChanWriteByCPU(0, 0x55);
    ASSERT_EQ(0, pBoard->ChanTxStateGetCPU(0)) << L"CPU transmitter is busy before RESET";

    tb.Run();

    for (int ch = 0; ch < 3; ch++)
        ASSERT_EQ(0200, pBoard->ChanTxStateGetCPU(ch)) << L"CPU transmitter is ready";
    for (int ch = 0; ch < 2; ch++)
        ASSERT_EQ(0, pBoard->ChanRxStateGetCPU(ch)) << L"CPU receiver is empty";
}

TEST(ProcessorSystemTests, Processor_Reset_InitializesTheSerialPorts)
{
    TestBoard tb;
    CMemoryController* pCtl = tb.Board()->GetCPUMemoryController();
    const uint16_t program[] = { OP_RESET };
    Prepare(tb, program, 1);

    pCtl->SetWord(0176570, false, 0100);     // receiver interrupt enable
    tb.CpuMem()->SerialInput(0x41);          // a byte is received
    pCtl->SetWord(0176576, false, 'A');      // a byte is sent, the transmitter is busy
    ASSERT_TRUE((pCtl->GetWord(0176570, false, false) & 0200) != 0) << L"receiver is ready before RESET";
    ASSERT_EQ(0, pCtl->GetWord(0176574, false, false) & 0200) << L"transmitter is busy before RESET";

    tb.Run();

    ASSERT_EQ(0, pCtl->GetWord(0176570, false, false)) << L"receiver status is cleared";
    ASSERT_EQ(0200, pCtl->GetWord(0176574, false, false)) << L"transmitter is ready";
}

TEST(ProcessorSystemTests, Processor_Reset_CancelsThePendingLineClockRequest)
{
    TestBoard tb;
    tb.StartCPU();
    tb.SetPpuWord(0177054, 01);  // unmask the 50 Hz event
    const uint16_t program[] = { OP_RESET };
    Prepare(tb, program, 1);
    tb.SetWord(0100, HANDLER);
    tb.SetWord(0102, 0);
    tb.CPU()->SetPSW(PRIORITY_BIT);  // the request waits, the priority blocks it

    tb.Board()->Tick50();
    tb.Run();  // RESET
    tb.CPU()->SetPSW(0);

    ASSERT_FALSE(tb.TakeInterrupt()) << L"the line clock request is gone";
}

TEST(ProcessorSystemTests, Processor_Reset_LineClockRequestStaysPendingWithoutReset)
{
    // The control for the previous test: without RESET the request is taken as soon as the priority allows
    TestBoard tb;
    tb.StartCPU();
    tb.SetPpuWord(0177054, 01);
    const uint16_t program[] = { OP_INC_R0 };
    Prepare(tb, program, 1);
    tb.SetWord(0100, HANDLER);
    tb.SetWord(0102, 0);
    tb.CPU()->SetPSW(PRIORITY_BIT);

    tb.Board()->Tick50();
    tb.Run();
    tb.CPU()->SetPSW(0);

    ASSERT_TRUE(tb.TakeInterrupt()) << L"the line clock request is taken";
    ASSERT_EQ(HANDLER, tb.CPU()->GetPC()) << L"PC = handler";
}
