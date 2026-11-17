/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "PreviewDebugger.h"
#include "emubase/Emubase.h"
#include "util/IniEncoding.h"
#include <gtest/gtest.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
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

TEST_F(PreviewDebuggerTest, EditsRegistersAndOnlyWritableAlignedRam)
{
    preview_debugger::Session debugger(*board);
    using preview_debugger::Processor;
    ASSERT_TRUE(debugger.WriteRegister(Processor::cpu, 0, 012345));
    EXPECT_EQ(board->GetCPU()->GetReg(0), 012345);
    ASSERT_TRUE(debugger.WriteRegister(Processor::ppu, 8, 00011));
    EXPECT_EQ(board->GetPPU()->GetPSW(), 00011);
    EXPECT_FALSE(debugger.WriteRegister(Processor::cpu, -1, 0));
    EXPECT_FALSE(debugger.WriteRegister(Processor::cpu, 9, 0));
    ASSERT_TRUE(debugger.WriteMemory(Processor::cpu, 01000, 012345));
    EXPECT_EQ(debugger.ReadWord(Processor::cpu, 01000, false), 012345);
    EXPECT_FALSE(debugger.WriteMemory(Processor::cpu, 01001, 0));
    const auto before = debugger.ReadPort(Processor::ppu, 0177700);
    EXPECT_FALSE(debugger.WriteMemory(Processor::ppu, 0177700, 0));
    EXPECT_EQ(debugger.ReadPort(Processor::ppu, 0177700), before);
}

TEST_F(PreviewDebuggerTest, InspectsRawPlanesRomAndProcessorMemoryWithoutDeviceReads)
{
    preview_debugger::Session debugger(*board);
    using preview_debugger::MemorySource;
    using preview_debugger::Processor;
    board->SetRAMWord(0, 01000, 0x1234);
    board->SetRAMWord(1, 01000, 0x5678);
    board->SetRAMWord(2, 01000, 0x9abc);
    EXPECT_EQ(debugger.InspectMemory(MemorySource::ram0, Processor::cpu, 01000, false), 0x1234);
    EXPECT_EQ(debugger.InspectMemory(MemorySource::ram1, Processor::cpu, 01000, false), 0x5678);
    EXPECT_EQ(debugger.InspectMemory(MemorySource::ram2, Processor::cpu, 01000, false), 0x9abc);
    EXPECT_EQ(debugger.InspectMemory(MemorySource::ram0, Processor::cpu, 01000, true), 0x34);
    EXPECT_EQ(debugger.InspectMemory(MemorySource::ram0, Processor::cpu, 01001, true), 0x12);
    EXPECT_FALSE(debugger.InspectMemory(MemorySource::ram0, Processor::cpu, 01001, false));
    EXPECT_FALSE(debugger.InspectMemory(MemorySource::rom, Processor::cpu, 077777, true));
    EXPECT_EQ(debugger.InspectMemory(MemorySource::rom, Processor::cpu, 0100000, false), board->GetROMWord(0));
    EXPECT_EQ(debugger.InspectMemory(MemorySource::rom, Processor::cpu, 0177777, true), board->GetROMByte(32767));
    EXPECT_EQ(debugger.InspectMemory(MemorySource::selected, Processor::cpu, 01000, false),
        debugger.InspectMemory(MemorySource::cpu, Processor::ppu, 01000, false));
    EXPECT_FALSE(debugger.InspectMemory(MemorySource::ppu, Processor::cpu, 0177700, false));
}

TEST_F(PreviewDebuggerTest, CurrentHintUsesCurrentProcessorAndFlags)
{
    preview_debugger::Session debugger(*board);
    auto* cpu = board->GetCPU();
    cpu->GetMemoryController()->SetWord(01000, false, 0001002); // BNE
    cpu->SetPSW(0004); // Z set: branch not taken.
    auto hint = debugger.CurrentHint(preview_debugger::Processor::cpu);
    EXPECT_TRUE(hint.isJump);
    EXPECT_FALSE(hint.jumpPredict);
    ASSERT_GT(hint.count, 0);
    EXPECT_EQ(hint.lines[0], L"Z=1");
    cpu->SetPSW(0);
    hint = debugger.CurrentHint(preview_debugger::Processor::cpu);
    EXPECT_TRUE(hint.jumpPredict);
    EXPECT_EQ(hint.lines[0], L"Z=0");
}

TEST_F(PreviewDebuggerTest, SavesMemoryViewAsLittleEndianAndRejectsBadRanges)
{
    const auto path = std::filesystem::temp_directory_path() /
        ("ukncbtl-debug-dump-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".bin");
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove(path, ec); } } cleanup{path};
    preview_debugger::Session debugger(*board);
    using preview_debugger::Processor;
    ASSERT_TRUE(debugger.SaveMemory(path, Processor::cpu, 01000, 01004));
    std::ifstream file(path, std::ios::binary);
    const std::string bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    EXPECT_EQ(bytes, std::string("\x80\x0a\xa0\x00", 4));
    EXPECT_EQ(debugger.SaveMemory(path, Processor::cpu, 01000, 01000).error(), preview_debugger::DumpError::invalid_range);
    EXPECT_EQ(debugger.SaveMemory(path / "missing", Processor::cpu, 01000, 01002).error(), preview_debugger::DumpError::file_error);
    file.close();
    ASSERT_TRUE(debugger.SaveMemory(path, Processor::cpu, 0, 0177777));
    EXPECT_EQ(std::filesystem::file_size(path), 65536u);
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

TEST_F(PreviewDebuggerTest, RunToUsesAndCancelsTemporaryTargets)
{
    preview_debugger::Session debugger(*board);
    using preview_debugger::Processor;
    EXPECT_FALSE(debugger.RunTo(Processor::cpu, 01001));
    ASSERT_TRUE(debugger.RunTo(Processor::cpu, 01002));
    EXPECT_FALSE(debugger.RunFrame());
    EXPECT_EQ(board->GetCPU()->GetPC(), 01002);
    EXPECT_FALSE(debugger.Breakpoints(Processor::cpu).Contains(01002));
}

TEST_F(PreviewDebuggerTest, LoadsUnicodeSubtitlesAndKeepsInstructionDecodingIndependent)
{
    const auto path = std::filesystem::temp_directory_path() /
        ("ukncbtl-debug-subtitles-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".lst");
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove(path, ec); } } cleanup{path};
    const auto bytes = ini_encoding::Encode(L";\u041f\u0440\u0438\u0432\u0435\u0442\r\n001000 .WORD ;\u0414\u0430\u043d\u043d\u044b\u0435\r\n001002 ;next\r\n",
        ini_encoding::Encoding::utf16_le);
    ASSERT_TRUE(bytes);
    {
        std::ofstream file(path, std::ios::binary);
        ASSERT_TRUE(file.write(reinterpret_cast<const char*>(bytes->data()), static_cast<std::streamsize>(bytes->size())));
    }
    preview_debugger::Session debugger(*board);
    ASSERT_TRUE(debugger.LoadSubtitles(path));
    ASSERT_NE(debugger.SubtitlePath(), nullptr);
    const auto lines = debugger.Disassemble(preview_debugger::Processor::cpu, 01000, 2, true);
    ASSERT_EQ(lines.size(), 2u);
    EXPECT_TRUE(lines[0].data);
    EXPECT_EQ(lines[0].instruction.mnemonic, L".WORD");
    EXPECT_EQ(lines[0].blockComment, ";\xd0\x9f\xd1\x80\xd0\xb8\xd0\xb2\xd0\xb5\xd1\x82");
    EXPECT_EQ(lines[0].comment, ";\xd0\x94\xd0\xb0\xd0\xbd\xd0\xbd\xd1\x8b\xd0\xb5");
    EXPECT_EQ(lines[1].address, 01002);
    EXPECT_EQ(lines[1].comment, ";next");
    EXPECT_EQ(debugger.Disassemble(preview_debugger::Processor::cpu, 01000, 1)[0].instruction.mnemonic, L"INC");
    EXPECT_FALSE(debugger.LoadSubtitles(path / "missing"));
    EXPECT_EQ(*debugger.SubtitlePath(), path);
    debugger.UnloadSubtitles();
    EXPECT_EQ(debugger.SubtitlePath(), nullptr);
    EXPECT_FALSE(debugger.Disassemble(preview_debugger::Processor::cpu, 01000, 1, true)[0].data);
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

TEST_F(PreviewDebuggerTest, TraceAndHistoryCanBeEnabledAndDisabledIndependently)
{
    const auto path = std::filesystem::temp_directory_path() /
        ("ukncbtl-trace-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".log");
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove(path, ec); } } cleanup{path};
    const auto read = [&] {
        std::ifstream file(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    };
    preview_debugger::Session debugger(*board);
    EXPECT_FALSE(debugger.StartTrace(path, 0));
    EXPECT_FALSE(debugger.StartTrace(path, 0040));
    EXPECT_FALSE(debugger.StartTrace(path / "missing", preview_debugger::TraceAll));
    ASSERT_TRUE(debugger.StartTrace(path, preview_debugger::TraceInstructionsCpu));
    EXPECT_TRUE(debugger.TraceConflicts(path));
    EXPECT_FALSE(debugger.StartTrace(path, preview_debugger::TraceAll));
    debugger.SetHistoryEnabled(true);
    debugger.Step();
    EXPECT_EQ(debugger.History().Length(true), 1);
    EXPECT_NE(read().find("CPU\t001000\tINC"), std::string::npos);
    debugger.SetHistoryEnabled(false);
    board->GetCPU()->SetPC(01000);
    debugger.Step();
    EXPECT_EQ(debugger.History().Length(true), 1);
    EXPECT_NE(read().find("CPU\t001000\tINC", read().find("CPU\t001000\tINC") + 1), std::string::npos);
    ASSERT_TRUE(debugger.ClearTrace());
    EXPECT_TRUE(read().empty());
    ASSERT_TRUE(debugger.SetTraceMask(preview_debugger::TraceInstructionsPpu));
    board->GetCPU()->SetPC(01000); debugger.Step();
    EXPECT_TRUE(read().empty());
    debugger.SetHistoryEnabled(true);
    debugger.StopTrace();
    board->GetCPU()->SetPC(01000); debugger.Step();
    EXPECT_EQ(debugger.History().Length(true), 2);
    EXPECT_FALSE(debugger.TraceConflicts(path));
    EXPECT_FALSE(debugger.TakeTraceError());
}

TEST_F(PreviewDebuggerTest, TraceCapturesFloppyAndRespectsMaskChanges)
{
    const auto path = std::filesystem::temp_directory_path() /
        ("ukncbtl-floppy-trace-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".log");
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove(path, ec); } } cleanup{path};
    const auto read = [&] {
        std::ifstream file(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    };
    {
        preview_debugger::Session debugger(*board);
        ASSERT_TRUE(debugger.StartTrace(path, preview_debugger::TraceFloppy | preview_debugger::TraceInstructionsCpu));
        debugger.SetHistoryEnabled(true);
        board->SetFloppyState(FLOPPY_CMD_DIR | FLOPPY_CMD_STEP);
        debugger.Step();
        const auto first = read();
        EXPECT_NE(first.find("Floppy COMMAND"), std::string::npos);
        EXPECT_NE(first.find("Floppy STEP"), std::string::npos);
        EXPECT_NE(first.find("CPU\t001000\tINC"), std::string::npos);
        EXPECT_EQ(debugger.History().Length(true), 1);
        ASSERT_TRUE(debugger.SetTraceMask(preview_debugger::TraceInstructionsCpu));
        board->SetFloppyState(FLOPPY_CMD_DIR | FLOPPY_CMD_STEP);
        EXPECT_EQ(read(), first);
        ASSERT_TRUE(debugger.SetTraceMask(preview_debugger::TraceFloppy));
        board->SetFloppyState(FLOPPY_CMD_DIR | FLOPPY_CMD_STEP);
        EXPECT_GT(read().size(), first.size());
        debugger.StopTrace();
        const auto stopped = read();
        board->SetFloppyState(FLOPPY_CMD_DIR | FLOPPY_CMD_STEP);
        EXPECT_EQ(read(), stopped);
    }
    board->SetFloppyState(FLOPPY_CMD_DIR | FLOPPY_CMD_STEP); // no callback to destroyed session
}

TEST_F(PreviewDebuggerTest, TraceRecordsSelectedInterrupts)
{
    const auto path = std::filesystem::temp_directory_path() /
        ("ukncbtl-trace-interrupt-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".log");
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove(path, ec); } } cleanup{path};
    preview_debugger::Session debugger(*board);
    ASSERT_TRUE(debugger.StartTrace(path, preview_debugger::TraceInterruptsCpu));
    auto* cpu = board->GetCPU();
    auto* memory = cpu->GetMemoryController();
    memory->SetWord(0100, false, 02000); memory->SetWord(0102, false, 00011);
    cpu->SetPSW(0); cpu->SetPC(01000); cpu->SetSP(0400); cpu->TickEVNT();
    ASSERT_TRUE(cpu->InterruptProcessing());
    std::ifstream file(path, std::ios::binary);
    const std::string bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    EXPECT_NE(bytes.find("CPU\t001000\tINT\t000100\tEVNT\t002000"), std::string::npos);
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
