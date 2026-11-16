/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "PreviewDebugger.h"
#include "emubase/Emubase.h"
#include <gtest/gtest.h>
#include <memory>

void DebugLog(const wchar_t*) {}
namespace
{
unsigned legacyInstructions = 0;
void LegacyInstruction(const CProcessor*, std::uint16_t, std::uint16_t) { ++legacyInstructions; }

class PreviewDebuggerTest : public testing::Test
{
protected:
    std::unique_ptr<CMotherboard> board;
    void SetUp() override
    {
        CProcessor::Init(); board = std::make_unique<CMotherboard>(); board->Reset();
        board->GetCPU()->SetDCLOPin(true); board->GetPPU()->SetDCLOPin(true);
        auto* cpu = board->GetCPU();
        cpu->SetACLOPin(true); cpu->SetDCLOPin(false); cpu->SetACLOPin(false); cpu->InterruptProcessing();
        cpu->SetPSW(0200); cpu->SetPC(01000); cpu->SetSP(0400); cpu->ClearInternalTick();
        auto* memory = cpu->GetMemoryController();
        memory->SetWord(01000, false, 0005200); // INC R0
        memory->SetWord(01002, false, 0000240); // NOP
        memory->SetWord(01004, false, 0000240);
    }
    void TearDown() override { board.reset(); CProcessor::Done(); }
};

TEST(PreviewDebugger, ParsesOnlyEvenOctalAddresses)
{
    EXPECT_EQ(preview_debugger::ParseAddress("0"), 0);
    EXPECT_EQ(preview_debugger::ParseAddress("177776"), 0177776);
    EXPECT_EQ(preview_debugger::ParseWord("177777"), 0177777);
    EXPECT_EQ(preview_debugger::ParseWord("3"), 3);
    for (const auto text : {"", "8", "-1", "3", "177777", "200000", "10x", " 10"})
        EXPECT_FALSE(preview_debugger::ParseAddress(text));
}

TEST_F(PreviewDebuggerTest, DisassemblyAndStepUseSelectedProcessorState)
{
    preview_debugger::Session debugger(*board);
    using preview_debugger::Processor;
    EXPECT_EQ(&debugger.GetProcessor(Processor::cpu), board->GetCPU());
    EXPECT_EQ(&debugger.GetProcessor(Processor::ppu), board->GetPPU());
    const auto lines = debugger.Disassemble(Processor::cpu, 01000, 2);
    ASSERT_EQ(lines.size(), 2u);
    EXPECT_EQ(lines[0].word, 0005200); EXPECT_EQ(lines[0].instruction.mnemonic, L"INC");
    EXPECT_EQ(lines[1].address, 01002);
    EXPECT_EQ(debugger.ReadWord(Processor::cpu, 01000, false), 0005200);
    EXPECT_EQ(debugger.ReadPort(Processor::cpu, 0176640),
        board->GetCPU()->GetMemoryController()->GetPortView(0176640));
    EXPECT_EQ(debugger.ReadPort(Processor::ppu, 0177700),
        board->GetPPU()->GetMemoryController()->GetPortView(0177700));
    debugger.Step();
    EXPECT_EQ(board->GetCPU()->GetReg(0), 1);
    EXPECT_EQ(board->GetCPU()->GetPC(), 01002);
}

TEST_F(PreviewDebuggerTest, BreakpointStopsFrameAndPreservesPermanentEntry)
{
    preview_debugger::Session debugger(*board);
    using preview_debugger::Processor;
    ASSERT_TRUE(debugger.Breakpoints(Processor::cpu).Add(01002));
    debugger.PrepareRun();
    EXPECT_FALSE(debugger.RunFrame());
    EXPECT_EQ(board->GetCPU()->GetPC(), 01002);
    EXPECT_EQ(board->GetCPU()->GetReg(0), 1);
    EXPECT_TRUE(debugger.Breakpoints(Processor::cpu).Contains(01002));
    EXPECT_TRUE(debugger.Breakpoints(Processor::cpu).Remove(01002));
}

TEST_F(PreviewDebuggerTest, StepOverUsesTemporaryBreakpointAndRetainsPermanentTarget)
{
    preview_debugger::Session debugger(*board);
    using preview_debugger::Processor;
    ASSERT_TRUE(debugger.Breakpoints(Processor::cpu).Add(01002));
    ASSERT_TRUE(debugger.StepOver(Processor::cpu));
    EXPECT_FALSE(debugger.RunFrame());
    EXPECT_EQ(board->GetCPU()->GetPC(), 01002);
    EXPECT_TRUE(debugger.Breakpoints(Processor::cpu).Contains(01002));
    ASSERT_TRUE(debugger.StepOver(Processor::cpu));
    EXPECT_FALSE(debugger.RunFrame());
    EXPECT_EQ(board->GetCPU()->GetPC(), 01004);
    EXPECT_FALSE(debugger.Breakpoints(Processor::cpu).Contains(01004));
    EXPECT_TRUE(debugger.Breakpoints(Processor::cpu).Contains(01002));
}

TEST_F(PreviewDebuggerTest, StepOverBranchFallsBackToDebugTick)
{
    preview_debugger::Session debugger(*board);
    board->GetCPU()->GetMemoryController()->SetWord(01000, false, 0000401); // BR +1
    EXPECT_FALSE(debugger.StepOver(preview_debugger::Processor::cpu));
    EXPECT_EQ(board->GetCPU()->GetPC(), 01004);
}

TEST_F(PreviewDebuggerTest, PpuBreakpointStopsTheSelectedProcessor)
{
    board->GetCPU()->SetDCLOPin(true);
    auto* ppu = board->GetPPU();
    ppu->SetACLOPin(true); ppu->SetDCLOPin(false); ppu->SetACLOPin(false); ppu->InterruptProcessing();
    ppu->SetPSW(0200); ppu->SetPC(01000); ppu->SetSP(0400); ppu->ClearInternalTick();
    ppu->GetMemoryController()->SetWord(01000, false, 0005200);
    preview_debugger::Session debugger(*board);
    ASSERT_TRUE(debugger.Breakpoints(preview_debugger::Processor::ppu).Add(01002));
    debugger.PrepareRun();
    EXPECT_FALSE(debugger.RunFrame());
    EXPECT_EQ(ppu->GetPC(), 01002);
    EXPECT_EQ(ppu->GetReg(0), 1);
}

TEST_F(PreviewDebuggerTest, StoppingStepOverCancelsOnlyTemporaryBreakpoint)
{
    preview_debugger::Session debugger(*board);
    using preview_debugger::Processor;
    ASSERT_TRUE(debugger.Breakpoints(Processor::cpu).Add(01004));
    ASSERT_TRUE(debugger.StepOver(Processor::cpu));
    EXPECT_TRUE(debugger.Breakpoints(Processor::cpu).Contains(01002));
    debugger.Stop();
    EXPECT_FALSE(debugger.Breakpoints(Processor::cpu).Contains(01002));
    EXPECT_TRUE(debugger.Breakpoints(Processor::cpu).Contains(01004));
}

TEST_F(PreviewDebuggerTest, HistoryCapturesInstructionsAndCanBeDisabledAndCleared)
{
    preview_debugger::Session debugger(*board);
    EXPECT_FALSE(debugger.HistoryEnabled());
    debugger.SetHistoryEnabled(true);
    debugger.Step();
    const auto& history = debugger.History();
    ASSERT_EQ(history.Length(true), 1);
    EXPECT_EQ(history.Length(false), 0);
    const auto record = history.Get(true, 0);
    EXPECT_FALSE(record.interrupt);
    EXPECT_EQ(record.pc, 01000);
    EXPECT_EQ(record.words[0], 0005200);
    EXPECT_EQ(record.words[1], 0000240);
    EXPECT_EQ(record.psw, 0200);
    board->GetCPU()->GetMemoryController()->SetWord(01002, false, 0005200);
    EXPECT_EQ(history.Get(true, 0).words[1], 0000240);
    debugger.SetHistoryEnabled(false);
    debugger.Step();
    EXPECT_EQ(history.Length(true), 1);
    debugger.ClearHistory();
    EXPECT_EQ(history.Length(true), 0);
}

TEST_F(PreviewDebuggerTest, HistoryCapturesInterruptsAfterNewStateIsLoaded)
{
    preview_debugger::Session debugger(*board);
    debugger.SetHistoryEnabled(true);
    auto* cpu = board->GetCPU();
    auto* memory = cpu->GetMemoryController();
    memory->SetWord(0100, false, 02000);
    memory->SetWord(0102, false, 00011);
    cpu->SetPSW(0); cpu->SetPC(01000); cpu->SetSP(0400);
    cpu->TickEVNT();
    ASSERT_TRUE(cpu->InterruptProcessing());
    const auto& history = debugger.History();
    ASSERT_EQ(history.Length(true), 1);
    const auto& record = history.Get(true, 0);
    EXPECT_TRUE(record.interrupt);
    EXPECT_EQ(record.pc, 01000);
    EXPECT_EQ(record.words[0], 0100);
    EXPECT_EQ(record.words[1], 02000);
    EXPECT_EQ(record.words[2], 00011);
}

TEST_F(PreviewDebuggerTest, HistorySeparatesProcessorsAndDetachesOnDestruction)
{
    {
        preview_debugger::Session debugger(*board);
        debugger.SetHistoryEnabled(true);
        for (auto* processor : {board->GetCPU(), board->GetPPU()})
        {
            processor->SetPSW(0200); processor->SetPC(01000);
            processor->GetMemoryController()->SetWord(01000, false, 0005200);
            processor->CommandExecution();
        }
        EXPECT_EQ(debugger.History().Length(true), 1);
        EXPECT_EQ(debugger.History().Length(false), 1);
        EXPECT_EQ(debugger.History().Get(false, 0).pc, 01000);
    }
    // Both board callbacks must be detached before the session is destroyed.
    board->GetCPU()->SetPC(01000);
    board->GetCPU()->CommandExecution();
    board->GetPPU()->SetPC(01000);
    board->GetPPU()->CommandExecution();
}

TEST_F(PreviewDebuggerTest, LegacyAndContextCallbacksReplaceEachOther)
{
    legacyInstructions = 0;
    auto* cpu = board->GetCPU();
    board->SetInstructionCallback(LegacyInstruction);
    cpu->CommandExecution();
    EXPECT_EQ(legacyInstructions, 1);
    preview_debugger::Session debugger(*board);
    debugger.SetHistoryEnabled(true);
    cpu->SetPC(01000); cpu->CommandExecution();
    EXPECT_EQ(legacyInstructions, 1);
    EXPECT_EQ(debugger.History().Length(true), 1);
    board->SetInstructionCallback(LegacyInstruction);
    cpu->SetPC(01000); cpu->CommandExecution();
    EXPECT_EQ(legacyInstructions, 2);
    EXPECT_EQ(debugger.History().Length(true), 1);
    debugger.SetHistoryEnabled(false);
    cpu->SetPC(01000); cpu->CommandExecution();
    EXPECT_EQ(legacyInstructions, 2);
}
}
