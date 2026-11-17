/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "PreviewConsole.h"
#include "emubase/Emubase.h"
#include <gtest/gtest.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>

void DebugLog(const wchar_t*) {}
namespace
{
class PreviewConsoleTest : public testing::Test
{
protected:
    std::unique_ptr<CMotherboard> board;
    std::unique_ptr<preview_debugger::Session> debugger;
    preview_console::Console console;
    preview_debugger::Processor processor = preview_debugger::Processor::cpu;
    void SetUp() override
    {
        CProcessor::Init(); board = std::make_unique<CMotherboard>(); board->Reset();
        board->GetCPU()->SetDCLOPin(true); board->GetPPU()->SetDCLOPin(true);
        auto* cpu = board->GetCPU();
        cpu->SetACLOPin(true); cpu->SetDCLOPin(false); cpu->SetACLOPin(false); cpu->InterruptProcessing();
        cpu->SetPSW(0200); cpu->SetPC(01000); cpu->SetSP(0400); cpu->ClearInternalTick();
        cpu->GetMemoryController()->SetWord(01000, false, 0005200);
        cpu->GetMemoryController()->SetWord(01002, false, 0000240);
        debugger = std::make_unique<preview_debugger::Session>(*board);
    }
    void TearDown() override { debugger.reset(); board.reset(); CProcessor::Done(); }
    preview_console::Action Submit(std::string_view command, bool running = false)
    { return console.Submit(command, *debugger, processor, running); }
};

TEST_F(PreviewConsoleTest, EditsAndInspectsSelectedProcessorWithOctalCommands)
{
    Submit("r0=12345");
    EXPECT_EQ(board->GetCPU()->GetReg(0), 012345);
    Submit("r0 000777", true);
    EXPECT_EQ(board->GetCPU()->GetReg(0), 012345);
    EXPECT_NE(console.Log().find("Pause the machine first"), std::string::npos);
    Submit("m01000=123456");
    EXPECT_EQ(debugger->ReadWord(processor, 01000, false), 0123456);
    Submit("m01000"); Submit("d01000"); Submit("r");
    EXPECT_NE(console.Log().find("001000  123456"), std::string::npos);
    Submit("p");
    EXPECT_EQ(processor, preview_debugger::Processor::ppu);
    Submit("rps=000011");
    EXPECT_EQ(board->GetPPU()->GetPSW(), 00011);
}

TEST_F(PreviewConsoleTest, BreakpointsAndExecutionReturnActionsForTheFrontend)
{
    using preview_console::Action;
    Submit("b01002");
    EXPECT_TRUE(debugger->Breakpoints(processor).Contains(01002));
    EXPECT_EQ(Submit("g"), Action::run);
    EXPECT_FALSE(debugger->RunFrame());
    EXPECT_EQ(board->GetCPU()->GetPC(), 01002);
    Submit("bc01002");
    EXPECT_FALSE(debugger->Breakpoints(processor).Contains(01002));
    EXPECT_EQ(Submit("g01004"), Action::run);
    EXPECT_FALSE(debugger->RunFrame());
    EXPECT_EQ(board->GetCPU()->GetPC(), 01004);
    EXPECT_EQ(Submit("pause", true), Action::pause);
}

TEST_F(PreviewConsoleTest, RejectsInvalidCommandsAndPreservesReadOnlyMemory)
{
    const auto before = board->GetCPU()->GetReg(0);
    for (auto command : {"r9=3", "r0=8", "g 3", "m 1001 1", "b 177777", "b 1000 extra", "unknown"})
        EXPECT_EQ(Submit(command), preview_console::Action::none);
    EXPECT_EQ(board->GetCPU()->GetReg(0), before);
    EXPECT_NE(console.Log().find("Invalid command"), std::string::npos);
    Submit("p");
    const auto port = debugger->ReadPort(processor, 0177700);
    Submit("m177700=1");
    EXPECT_EQ(debugger->ReadPort(processor, 0177700), port);
    EXPECT_NE(console.Log().find("not writable RAM"), std::string::npos);
}

TEST_F(PreviewConsoleTest, DumpsMemoryAtSelectedRegisterAndValidatesAddresses)
{
    Submit("r0=001000");
    Submit("mr0");
    EXPECT_NE(console.Log().find("001000  005200"), std::string::npos);
    Submit("c");
    Submit("r0=001001");
    Submit("mr0");
    EXPECT_NE(console.Log().find("Invalid command"), std::string::npos);
    Submit("c");
    Submit("mr8");
    EXPECT_NE(console.Log().find("Invalid command"), std::string::npos);
    Submit("c");
    Submit("mr0 1000");
    EXPECT_NE(console.Log().find("Invalid command"), std::string::npos);
}

TEST_F(PreviewConsoleTest, ConvertsFisWordsAndSupportsShortDisassembly)
{
    Submit("fc000000 000000");
    EXPECT_NE(console.Log().find("exp: 0, mant: 0x800000\n0"), std::string::npos);
    Submit("c");
    Submit("fc 040200 000000");
    EXPECT_NE(console.Log().find("exp: 1, mant: 0x800000"), std::string::npos);
    Submit("c");
    Submit("fc 8 0");
    EXPECT_NE(console.Log().find("Invalid command"), std::string::npos);
    Submit("c");
    Submit("D01000");
    EXPECT_NE(console.Log().find("001000  INC"), std::string::npos);
    EXPECT_EQ(console.Log().find("001000  005200"), std::string::npos);
    Submit("r");
    EXPECT_NE(console.Log().find("PC'="), std::string::npos);
}

TEST_F(PreviewConsoleTest, StepAndStepOverFollowDebuggerExecutionRules)
{
    using preview_console::Action;
    EXPECT_EQ(Submit("s"), Action::none);
    EXPECT_EQ(board->GetCPU()->GetPC(), 01002);
    EXPECT_EQ(board->GetCPU()->GetReg(0), 1);
    board->GetCPU()->GetMemoryController()->SetWord(01004, false, 0000240);
    EXPECT_EQ(Submit("so"), Action::run);
    EXPECT_FALSE(debugger->RunFrame());
    EXPECT_EQ(board->GetCPU()->GetPC(), 01004);
    EXPECT_EQ(Submit("s", true), Action::none);
    EXPECT_EQ(board->GetCPU()->GetPC(), 01004);
}

TEST_F(PreviewConsoleTest, HistoryIsBoundedAndLogSavesAsUtf8)
{
    Submit("r"); Submit("b"); Submit("r");
    EXPECT_EQ(console.Navigate(-1), "r");
    EXPECT_EQ(console.Navigate(-1), "b");
    EXPECT_EQ(console.Navigate(1), "r");
    EXPECT_TRUE(console.Navigate(1).empty());
    for (int i = 0; i < 600; ++i) Submit("h");
    Submit("unknown \xd0\x9f\xd1\x80\xd0\xb8\xd0\xb2\xd0\xb5\xd1\x82");
    EXPECT_NE(console.Log().find("\xd0\x9f\xd1\x80\xd0\xb8\xd0\xb2\xd0\xb5\xd1\x82"), std::string::npos);
    EXPECT_LE(console.Log().size(), 65536u);
    auto path = std::filesystem::temp_directory_path() /
        ("ukncbtl-console-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    path += std::filesystem::path(u8"-\u65e5.txt");
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove(path, ec); } } cleanup{path};
    ASSERT_TRUE(console.Save(path));
    std::ifstream file(path, std::ios::binary);
    const std::string data{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    EXPECT_EQ(data, console.Log());
    EXPECT_FALSE(console.Save(path / "missing"));
    Submit("c"); EXPECT_TRUE(console.Log().empty());
}
}
