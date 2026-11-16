/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "PreviewDebugger.h"
#include "DebugStep.h"
#include "FrameControl.h"
#include "emubase/Board.h"
#include "emubase/Processor.h"
#include <array>
#include <charconv>
#include <utility>

std::optional<std::uint16_t> preview_debugger::ParseWord(std::string_view text)
{
    if (text.empty()) return std::nullopt;
    unsigned value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, 8);
    if (error != std::errc{} || end != text.data() + text.size() || value > 0177777)
        return std::nullopt;
    return static_cast<std::uint16_t>(value);
}

std::optional<std::uint16_t> preview_debugger::ParseAddress(std::string_view text)
{
    const auto value = ParseWord(text);
    return value && !(*value & 1) ? value : std::nullopt;
}

preview_debugger::Session::~Session()
{
    SetHistoryEnabled(false);
    board_.SetCPUBreakpoints({}); board_.SetPPUBreakpoints({});
}
CProcessor& preview_debugger::Session::GetProcessor(Processor which) const
{ return *(which == Processor::cpu ? board_.GetCPU() : board_.GetPPU()); }
BreakpointSet& preview_debugger::Session::Breakpoints(Processor which)
{ return which == Processor::cpu ? cpu_ : ppu_; }
std::uint16_t preview_debugger::Session::ReadWord(Processor which, std::uint16_t address, bool instruction) const
{
    auto& processor = GetProcessor(which);
    return processor.GetMemoryController()->GetWordView(address, processor.IsHaltMode(), instruction).value;
}
std::uint16_t preview_debugger::Session::ReadPort(Processor which, std::uint16_t address) const
{ return GetProcessor(which).GetMemoryControllerConst()->GetPortView(address); }
std::vector<preview_debugger::Line> preview_debugger::Session::Disassemble(Processor which, std::uint16_t address, unsigned count) const
{
    std::vector<Line> lines;
    lines.reserve(count);
    for (unsigned index = 0; index < count; ++index)
    {
        std::array<std::uint16_t, 3> words{};
        for (unsigned word = 0; word < words.size(); ++word)
            words[word] = ReadWord(which, static_cast<std::uint16_t>(address + word * 2), true);
        auto decoded = DisassembleInstruction(words, address);
        if (!decoded) break;
        const auto length = decoded->length;
        lines.push_back({address, words[0], std::move(*decoded)});
        address = static_cast<std::uint16_t>(address + length * 2);
    }
    return lines;
}
void preview_debugger::Session::PrepareRun() { frame_control::PrepareRun(board_, cpu_, ppu_); }
void preview_debugger::Session::Stop() { frame_control::CancelTemporaryBreakpoints(cpu_, ppu_); }
void preview_debugger::Session::Step() { Stop(); board_.DebugTicks(); }
bool preview_debugger::Session::StepOver(Processor which)
{
    Stop();
    const auto pc = GetProcessor(which).GetPC();
    const auto lines = Disassemble(which, pc, 1);
    const auto target = lines.empty() ? std::nullopt : debug_step::StepOverTarget(lines[0].word, pc, lines[0].instruction.length);
    if (!target || *target == BreakpointSet::NoBreakpoint) { Step(); return false; }
    Breakpoints(which).SetTemporary(*target);
    PrepareRun();
    return true;
}
bool preview_debugger::Session::RunFrame() { return frame_control::RunFrame(board_, cpu_, ppu_); }

void preview_debugger::Session::SetHistoryEnabled(bool enabled)
{
    if (historyEnabled_ == enabled) return;
    board_.SetInstructionCallbackWithContext(enabled ? OnInstruction : nullptr, enabled ? this : nullptr);
    board_.SetInterruptCallbackWithContext(enabled ? OnInterrupt : nullptr, enabled ? this : nullptr);
    historyEnabled_ = enabled;
}

void preview_debugger::Session::OnInstruction(void* context, const CProcessor* processor, std::uint16_t address, std::uint16_t opcode)
{
    auto& session = *static_cast<Session*>(context);
    const auto* memory = processor->GetMemoryControllerConst();
    const bool halt = processor->IsHaltMode();
    const std::array<std::uint16_t, 3> words{opcode,
        memory->GetWordView(static_cast<std::uint16_t>(address + 2), halt, true).value,
        memory->GetWordView(static_cast<std::uint16_t>(address + 4), halt, true).value};
    session.history_.RecordInstruction(processor == session.board_.GetCPU(), address, words, processor->GetPSW());
}

void preview_debugger::Session::OnInterrupt(void* context, const CProcessor* processor, std::uint16_t oldPc, std::uint16_t vector)
{
    auto& session = *static_cast<Session*>(context);
    session.history_.RecordInterrupt(processor == session.board_.GetCPU(), oldPc, vector,
        processor->GetPC(), processor->GetPSW());
}
