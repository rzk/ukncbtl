/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "PreviewDebugger.h"
#include "DebugStep.h"
#include "FrameControl.h"
#include "MemoryDump.h"
#include "emubase/Board.h"
#include "emubase/Processor.h"
#include "util/IniEncoding.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <format>
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

std::optional<std::uint16_t> preview_debugger::ParseMemoryValue(std::string_view text, bool hex, bool byteMode)
{
    if (text.empty()) return std::nullopt;
    unsigned value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, hex ? 16 : 8);
    if (error != std::errc{} || end != text.data() + text.size() || value > (byteMode ? 0xffu : 0xffffu))
        return std::nullopt;
    return static_cast<std::uint16_t>(value);
}

std::string preview_debugger::FormatValue(std::uint16_t value, bool hex, bool byteMode)
{
    if (byteMode) value &= 0xff;
    if (hex) return byteMode ? std::format("{:02X}", value) : std::format("{:04X}", value);
    return byteMode ? std::format("{:03o}", value) : std::format("{:06o}", value);
}

std::optional<std::string> preview_debugger::ToUtf8(std::wstring_view text)
{
    const auto bytes = ini_encoding::Encode(text, ini_encoding::Encoding::utf8);
    if (!bytes) return std::nullopt;
    return std::string(bytes->begin(), bytes->end());
}

struct preview_debugger::Session::Comparison
{
    std::array<std::array<std::uint16_t, 11>, 2> registers{};
    // One view each for CPU, PPU, the three raw RAM planes, and ROM.
    std::array<std::vector<std::optional<std::uint16_t>>, 6> memory;
};

preview_debugger::Session::Session(CMotherboard& board) : board_(board) {}

preview_debugger::Session::~Session()
{
    StopTrace();
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

std::optional<std::uint16_t> preview_debugger::Session::InspectMemory(
    MemorySource source, Processor selected, std::uint16_t address, bool byteMode) const
{
    const auto aligned = static_cast<std::uint16_t>(address & 0xfffe);
    if (!byteMode && address != aligned) return std::nullopt;
    std::uint16_t word = 0;
    if (source == MemorySource::ram0 || source == MemorySource::ram1 || source == MemorySource::ram2)
        word = board_.GetRAMWord(static_cast<int>(source) - static_cast<int>(MemorySource::ram0), aligned);
    else if (source == MemorySource::rom)
    {
        if (aligned < 0100000) return std::nullopt;
        word = board_.GetROMWord(static_cast<std::uint16_t>(aligned - 0100000));
    }
    else if (source == MemorySource::selected || source == MemorySource::cpu || source == MemorySource::ppu)
    {
        const auto which = source == MemorySource::selected ? selected :
            source == MemorySource::cpu ? Processor::cpu : Processor::ppu;
        const auto& processor = GetProcessor(which);
        const auto view = processor.GetMemoryControllerConst()->GetWordView(aligned, processor.IsHaltMode(), false);
        if (view.addressType >= ADDRTYPE_IO) return std::nullopt;
        word = view.value;
    }
    else return std::nullopt;
    return byteMode ? static_cast<std::uint16_t>((word >> ((address & 1) * 8)) & 0xff) : word;
}

std::optional<std::uint16_t> preview_debugger::Session::FindMemory(MemorySource source, Processor selected,
    std::uint16_t start, std::uint16_t value, bool byteMode, SearchDirection direction) const
{
    if ((!byteMode && (start & 1)) || (byteMode && value > 0xff)) return std::nullopt;
    const unsigned stride = byteMode ? 1 : 2;
    auto address = start;
    for (unsigned index = 0; index < 65536u / stride; ++index)
    {
        address = static_cast<std::uint16_t>(direction == SearchDirection::forward ? address + stride : address - stride);
        if (const auto cell = InspectMemory(source, selected, address, byteMode); cell && *cell == value)
            return address;
    }
    return std::nullopt;
}

namespace
{
constexpr std::array comparisonSources{preview_debugger::MemorySource::cpu, preview_debugger::MemorySource::ppu,
    preview_debugger::MemorySource::ram0, preview_debugger::MemorySource::ram1,
    preview_debugger::MemorySource::ram2, preview_debugger::MemorySource::rom};

std::uint16_t RegisterValue(const CProcessor& processor, int index)
{
    if (index < 8) return processor.GetReg(index);
    if (index == 8) return processor.GetPSW();
    if (index == 9) return processor.GetCPC();
    return processor.GetCPSW();
}
}

void preview_debugger::Session::CaptureComparison()
{
    auto capture = std::make_unique<Comparison>();
    for (int processor = 0; processor < 2; ++processor)
        for (int reg = 0; reg < 11; ++reg)
            capture->registers[processor][reg] = RegisterValue(GetProcessor(processor ? Processor::ppu : Processor::cpu), reg);
    for (std::size_t view = 0; view < comparisonSources.size(); ++view)
    {
        auto& values = capture->memory[view];
        values.reserve(32768);
        for (unsigned address = 0; address < 65536; address += 2)
            values.push_back(InspectMemory(comparisonSources[view], Processor::cpu,
                static_cast<std::uint16_t>(address), false));
    }
    comparison_ = std::move(capture);
}

void preview_debugger::Session::ClearComparison() { comparison_.reset(); }
bool preview_debugger::Session::HasComparison() const { return static_cast<bool>(comparison_); }

bool preview_debugger::Session::RegisterChanged(Processor which, int index) const
{
    if (!comparison_ || index < 0 || index >= 11) return false;
    const auto processor = which == Processor::cpu ? 0 : 1;
    return comparison_->registers[processor][index] != RegisterValue(GetProcessor(which), index);
}

bool preview_debugger::Session::MemoryChanged(MemorySource source, Processor selected,
    std::uint16_t address, bool byteMode) const
{
    if (!comparison_ || (!byteMode && (address & 1))) return false;
    if (source == MemorySource::selected) source = selected == Processor::cpu ? MemorySource::cpu : MemorySource::ppu;
    const auto it = std::find(comparisonSources.begin(), comparisonSources.end(), source);
    if (it == comparisonSources.end()) return false;
    const auto view = static_cast<std::size_t>(it - comparisonSources.begin());
    const auto previous = comparison_->memory[view][address / 2];
    const auto current = InspectMemory(source, selected, static_cast<std::uint16_t>(address & 0xfffe), false);
    if (!previous || !current) return false;
    const auto difference = static_cast<std::uint16_t>(*previous ^ *current);
    return byteMode ? ((difference >> ((address & 1) * 8)) & 0xff) != 0 : difference != 0;
}

DisasmHint preview_debugger::Session::CurrentHint(Processor which) const
{
    const auto& processor = GetProcessor(which);
    const auto* memory = processor.GetMemoryControllerConst();
    const auto pc = processor.GetPC();
    const std::array<std::uint16_t, 3> words{
        ReadWord(which, pc, true), ReadWord(which, static_cast<std::uint16_t>(pc + 2), true),
        ReadWord(which, static_cast<std::uint16_t>(pc + 4), true)};
    return Disasm_GetHint(words, processor, *memory);
}

bool preview_debugger::Session::WriteRegister(Processor which, int index, std::uint16_t value)
{
    auto& processor = GetProcessor(which);
    if (index == 8) processor.SetPSW(value);
    else if (index >= 0 && index < 8) processor.SetReg(index, value);
    else return false;
    return true;
}
bool preview_debugger::Session::WriteMemory(Processor which, std::uint16_t address, std::uint16_t value)
{
    return WriteMemory(MemorySource::selected, which, address, value, false);
}
bool preview_debugger::Session::WriteMemory(MemorySource source, Processor selected,
    std::uint16_t address, std::uint16_t value, bool byteMode)
{
    if ((!byteMode && (address & 1)) || (byteMode && value > 0xff)) return false;
    if (source == MemorySource::ram0 || source == MemorySource::ram1 || source == MemorySource::ram2)
    {
        const int plane = static_cast<int>(source) - static_cast<int>(MemorySource::ram0);
        if (byteMode) board_.SetRAMByte(plane, address, static_cast<std::uint8_t>(value));
        else board_.SetRAMWord(plane, address, value);
        return true;
    }
    if (source != MemorySource::selected && source != MemorySource::cpu && source != MemorySource::ppu) return false;
    const auto which = source == MemorySource::selected ? selected : source == MemorySource::cpu ? Processor::cpu : Processor::ppu;
    auto& processor = GetProcessor(which);
    auto* memory = processor.GetMemoryController();
    const auto type = memory->GetWordView(static_cast<std::uint16_t>(address & 0xfffe), processor.IsHaltMode(), false).addressType;
    if (type != ADDRTYPE_RAM0 && type != ADDRTYPE_RAM1 && type != ADDRTYPE_RAM2 && type != ADDRTYPE_RAM12)
        return false;
    if (byteMode) memory->SetByte(address, processor.IsHaltMode(), static_cast<std::uint8_t>(value));
    else memory->SetWord(address, processor.IsHaltMode(), value);
    return true;
}
std::expected<void, preview_debugger::DumpError> preview_debugger::Session::SaveMemory(
    const std::filesystem::path& path, Processor which, std::uint16_t start, std::uint16_t end) const
{
    return SaveMemory(path, MemorySource::selected, which, start, end);
}
std::expected<void, preview_debugger::DumpError> preview_debugger::Session::SaveMemory(
    const std::filesystem::path& path, MemorySource source, Processor selected, std::uint16_t start, std::uint16_t end) const
{
    const auto range = memory_dump::Range(start, end);
    if (!range || static_cast<int>(source) < static_cast<int>(MemorySource::selected) ||
        static_cast<int>(source) > static_cast<int>(MemorySource::rom)) return std::unexpected(DumpError::invalid_range);
    std::vector<std::uint16_t> words(range->Count());
    for (std::size_t index = 0; index < words.size(); ++index)
        words[index] = InspectMemory(source, selected, static_cast<std::uint16_t>((range->first + index) * 2), false).value_or(0);
    if (!memory_dump::Save(path, words)) return std::unexpected(DumpError::file_error);
    return {};
}
std::vector<preview_debugger::Line> preview_debugger::Session::Disassemble(Processor which, std::uint16_t address,
    unsigned count, bool useSubtitles) const
{
    std::vector<Line> lines;
    lines.reserve(count);
    for (unsigned index = 0; index < count; ++index)
    {
        std::array<std::uint16_t, 3> words{};
        for (unsigned word = 0; word < words.size(); ++word)
            words[word] = ReadWord(which, static_cast<std::uint16_t>(address + word * 2), true);
        const auto* block = useSubtitles ? subtitles::Find(subtitles_, address, SUBTYPE_BLOCKCOMMENT) : nullptr;
        const auto* subtitle = useSubtitles ? subtitles::Find(subtitles_, address, SUBTYPE_COMMENT | SUBTYPE_DATA) : nullptr;
        const bool data = subtitle && (subtitle->type & SUBTYPE_DATA);
        auto decoded = data ? std::optional{DisassembledInstruction{L".WORD", std::format(L"{:06o}", words[0]), 1}} :
            DisassembleInstruction(words, address);
        if (!decoded) break;
        const auto length = decoded->length;
        lines.push_back({address, words[0], std::move(*decoded),
            block ? ToUtf8(block->comment).value_or("?") : "",
            subtitle ? ToUtf8(subtitle->comment).value_or("?") : "", data});
        address = static_cast<std::uint16_t>(address + length * 2);
    }
    return lines;
}
std::expected<void, subtitles::FileError> preview_debugger::Session::LoadSubtitles(const std::filesystem::path& path)
{
    auto loaded = subtitles::LoadFile(path);
    if (!loaded) return std::unexpected(loaded.error());
    auto savedPath = path;
    subtitles_ = std::move(*loaded); subtitlePath_ = std::move(savedPath); subtitlesLoaded_ = true;
    return {};
}
void preview_debugger::Session::UnloadSubtitles()
{ subtitles_.clear(); subtitlePath_.clear(); subtitlesLoaded_ = false; }
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
bool preview_debugger::Session::RunTo(Processor which, std::uint16_t address)
{
    if ((address & 1) || address == BreakpointSet::NoBreakpoint) return false;
    Stop(); Breakpoints(which).SetTemporary(address); PrepareRun();
    return true;
}

void preview_debugger::Session::SetHistoryEnabled(bool enabled)
{
    if (historyEnabled_ == enabled) return;
    historyEnabled_ = enabled;
    UpdateCallbacks();
}

void preview_debugger::Session::UpdateCallbacks()
{
    const bool enabled = historyEnabled_ || trace_.has_value();
    board_.SetInstructionCallbackWithContext(enabled ? OnInstruction : nullptr, enabled ? this : nullptr);
    board_.SetInterruptCallbackWithContext(enabled ? OnInterrupt : nullptr, enabled ? this : nullptr);
    const bool floppy = (traceMask_ & TraceFloppy) != 0;
    board_.SetFloppyTraceCallback(floppy ? OnFloppyTrace : nullptr, floppy ? this : nullptr);
}

bool preview_debugger::Session::StartTrace(const std::filesystem::path& path, std::uint16_t mask)
{
    if (trace_ || path.empty() || !mask || (mask & ~TraceAll)) return false;
    auto savedPath = path;
    trace_.emplace(path);
    if (!trace_->Open()) { trace_.reset(); return false; }
    tracePath_ = std::move(savedPath);
    traceMask_ = mask;
    traceError_ = false;
    UpdateCallbacks();
    return true;
}

bool preview_debugger::Session::SetTraceMask(std::uint16_t mask)
{
    if (!trace_ || !mask || (mask & ~TraceAll)) return false;
    traceMask_ = mask;
    UpdateCallbacks();
    return true;
}

void preview_debugger::Session::StopTrace()
{
    traceMask_ = 0;
    trace_.reset();
    tracePath_.clear();
    UpdateCallbacks();
}

bool preview_debugger::Session::ClearTrace()
{
    if (!trace_ || !traceMask_) return false;
    if (trace_->Clear()) return true;
    traceError_ = true; traceMask_ = 0;
    return false;
}

bool preview_debugger::Session::TraceConflicts(const std::filesystem::path& path) const
{
    if (!trace_ || path.empty()) return false;
    if (tracePath_ == path) return true;
    std::error_code error;
    return std::filesystem::equivalent(tracePath_, path, error) && !error;
}

bool preview_debugger::Session::TakeTraceError()
{
    if (!std::exchange(traceError_, false)) return false;
    StopTrace();
    return true;
}

void preview_debugger::Session::TraceWrite(std::string_view text)
{
    if (trace_ && !trace_->Append(text)) { traceError_ = true; traceMask_ = 0; }
}

void preview_debugger::Session::OnFloppyTrace(void* context, std::wstring_view text)
{
    auto& session = *static_cast<Session*>(context);
    if (!(session.traceMask_ & TraceFloppy)) return;
    const auto utf8 = ToUtf8(text);
    if (utf8) session.TraceWrite(*utf8);
    else { session.traceError_ = true; session.traceMask_ = 0; }
}

void preview_debugger::Session::OnInstruction(void* context, const CProcessor* processor, std::uint16_t address, std::uint16_t opcode)
{
    auto& session = *static_cast<Session*>(context);
    const auto* memory = processor->GetMemoryControllerConst();
    const bool halt = processor->IsHaltMode();
    const std::array<std::uint16_t, 3> words{opcode,
        memory->GetWordView(static_cast<std::uint16_t>(address + 2), halt, true).value,
        memory->GetWordView(static_cast<std::uint16_t>(address + 4), halt, true).value};
    const bool cpu = processor == session.board_.GetCPU();
    if (session.historyEnabled_) session.history_.RecordInstruction(cpu, address, words, processor->GetPSW());
    if (session.traceMask_ & (cpu ? TraceInstructionsCpu : TraceInstructionsPpu))
    {
        const auto decoded = DisassembleInstruction(words, address);
        if (decoded) session.TraceWrite(std::format("{}\t{:06o}\t{}\t{}\n", cpu ? "CPU" : "PPU", address,
            ToUtf8(decoded->mnemonic).value_or("?"), ToUtf8(decoded->argument).value_or("?")));
    }
}

void preview_debugger::Session::OnInterrupt(void* context, const CProcessor* processor, std::uint16_t oldPc, std::uint16_t vector)
{
    auto& session = *static_cast<Session*>(context);
    const bool cpu = processor == session.board_.GetCPU();
    if (session.historyEnabled_) session.history_.RecordInterrupt(cpu, oldPc, vector, processor->GetPC(), processor->GetPSW());
    if (session.traceMask_ & (cpu ? TraceInterruptsCpu : TraceInterruptsPpu))
        session.TraceWrite(std::format("{}\t{:06o}\tINT\t{:06o}\t{}\t{:06o}\n", cpu ? "CPU" : "PPU", oldPc,
            vector, ToUtf8(InstructionHistory::InterruptName(vector)).value_or("?"), processor->GetPC()));
}
