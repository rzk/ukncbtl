/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "PreviewDebugger.h"
#include "emubase/Emubase.h"
#include <gtest/gtest.h>
#include <memory>

void DebugLog(const wchar_t*) {}
namespace
{
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
}
