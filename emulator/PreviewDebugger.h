/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once
#include "BreakpointSet.h"
#include "InstructionHistory.h"
#include "SubtitleFile.h"
#include "util/DebugLogFile.h"
#include "emubase/Disasm.h"
#include <array>
#include <bitset>
#include <cstdint>
#include <optional>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

class CMotherboard;
class CProcessor;
class FrameWorker;
namespace preview_debugger
{
enum class Processor { cpu, ppu };
enum class MemorySource { selected, cpu, ppu, ram0, ram1, ram2, rom };
enum class SearchDirection { forward, backward };
enum class DumpError { invalid_range, file_error };
// These match the legacy CPU/PPU instruction and interrupt trace masks.
inline constexpr std::uint16_t TraceFloppy = 0100, TraceInstructionsPpu = 0200, TraceInstructionsCpu = 0400;
inline constexpr std::uint16_t TraceInterruptsPpu = 01000, TraceInterruptsCpu = 02000;
inline constexpr std::uint16_t TraceAll = 03700;
struct Line
{
    std::uint16_t address, word;
    DisassembledInstruction instruction;
    std::string blockComment, comment;
    bool data = false;
};
[[nodiscard]] std::optional<std::uint16_t> ParseAddress(std::string_view text);
[[nodiscard]] std::optional<std::uint16_t> ParseWord(std::string_view text);
[[nodiscard]] std::optional<std::uint16_t> ParseMemoryValue(std::string_view text, bool hex, bool byteMode);
[[nodiscard]] std::string FormatValue(std::uint16_t value, bool hex, bool byteMode = false);
[[nodiscard]] std::optional<std::string> ToUtf8(std::wstring_view text);

// An owned debugger memory view. Capture on the board owner, then read on any
// thread. The accessibility mask excludes I/O and unmapped words.
class MemoryView
{
public:
    [[nodiscard]] std::optional<std::uint16_t> Inspect(std::uint16_t address, bool byteMode) const;
    [[nodiscard]] std::optional<std::uint16_t> Find(std::uint16_t start, std::uint16_t value,
        bool byteMode, SearchDirection direction) const;
private:
    friend class Session;
    std::array<std::uint8_t, 65536> bytes_{};
    std::bitset<65536 / 2> readable_;
};

// Owns breakpoint storage borrowed by the board. The board must outlive it.
// Calls on the session and board must be serialized with RunFrame. Trace output
// from a frame may finish afterward; ClearTrace and StopTrace drain it.
class Session
{
public:
    explicit Session(CMotherboard& board);
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    [[nodiscard]] CProcessor& GetProcessor(Processor which) const;
    [[nodiscard]] BreakpointSet& Breakpoints(Processor which);
    [[nodiscard]] std::uint16_t ReadWord(Processor which, std::uint16_t address, bool instruction) const;
    [[nodiscard]] std::uint16_t ReadPort(Processor which, std::uint16_t address) const;
    // Debugger views do not perform device reads. I/O and inaccessible memory
    // return no value, as does ROM outside its mapped 32 KiB range.
    [[nodiscard]] std::optional<std::uint16_t> InspectMemory(MemorySource source, Processor selected,
        std::uint16_t address, bool byteMode) const;
    // Search starts after the given address, wraps once, and skips inaccessible memory.
    [[nodiscard]] std::optional<std::uint16_t> FindMemory(MemorySource source, Processor selected,
        std::uint16_t start, std::uint16_t value, bool byteMode, SearchDirection direction) const;
    [[nodiscard]] std::unique_ptr<const MemoryView> CaptureMemoryView(MemorySource source, Processor selected) const;
    // Comparison is explicit and remains stable until recaptured or cleared.
    void CaptureComparison();
    void ClearComparison();
    [[nodiscard]] bool HasComparison() const;
    // Registers 0..7, PSW, CPC, CPSW are indexed 0..10.
    [[nodiscard]] bool RegisterChanged(Processor which, int index) const;
    [[nodiscard]] bool MemoryChanged(MemorySource source, Processor selected,
        std::uint16_t address, bool byteMode) const;
    [[nodiscard]] DisasmHint CurrentHint(Processor which) const;
    // Register 0..7 selects R0..PC; index 8 selects PSW.
    [[nodiscard]] bool WriteRegister(Processor which, int index, std::uint16_t value);
    // Only aligned RAM words are writable; ROM and I/O are never touched.
    [[nodiscard]] bool WriteMemory(Processor which, std::uint16_t address, std::uint16_t value);
    [[nodiscard]] bool WriteMemory(MemorySource source, Processor selected, std::uint16_t address,
        std::uint16_t value, bool byteMode);
    // The end address is exclusive; endpoints are rounded to whole words.
    [[nodiscard]] std::expected<void, DumpError> SaveMemory(const std::filesystem::path& path,
        Processor which, std::uint16_t start, std::uint16_t end) const;
    // Inaccessible words are written as zero; no device reads are performed.
    [[nodiscard]] std::expected<void, DumpError> SaveMemory(const std::filesystem::path& path,
        MemorySource source, Processor selected, std::uint16_t start, std::uint16_t end) const;
    [[nodiscard]] std::expected<std::vector<std::uint16_t>, DumpError> CaptureMemory(
        MemorySource source, Processor selected, std::uint16_t start, std::uint16_t end) const;
    [[nodiscard]] std::vector<Line> Disassemble(Processor which, std::uint16_t address, unsigned count,
        bool useSubtitles = false) const;
    [[nodiscard]] std::expected<void, subtitles::FileError> LoadSubtitles(const std::filesystem::path& path);
    void SetSubtitles(std::filesystem::path path, std::vector<DisasmSubtitleItem> items);
    void UnloadSubtitles();
    [[nodiscard]] const std::filesystem::path* SubtitlePath() const { return subtitlesLoaded_ ? &subtitlePath_ : nullptr; }
    void PrepareRun();
    void Stop();
    void Step();
    // True means a temporary breakpoint was installed and the machine should run.
    // False means step-over fell back to a single debug tick.
    [[nodiscard]] bool StepOver(Processor which);
    [[nodiscard]] bool RunTo(Processor which, std::uint16_t address);
    [[nodiscard]] bool RunFrame();
    void SetHistoryEnabled(bool enabled);
    [[nodiscard]] bool HistoryEnabled() const { return historyEnabled_; }
    void ClearHistory() { history_.Clear(); }
    [[nodiscard]] const InstructionHistory& History() const { return history_; }
    [[nodiscard]] bool StartTrace(const std::filesystem::path& path, std::uint16_t mask);
    [[nodiscard]] bool SetTraceMask(std::uint16_t mask);
    void StopTrace();
    [[nodiscard]] bool ClearTrace();
    [[nodiscard]] std::uint16_t TraceMask() const { return traceMask_; }
    [[nodiscard]] const std::filesystem::path* TracePath() const { return trace_ ? &tracePath_ : nullptr; }
    [[nodiscard]] bool TraceConflicts(const std::filesystem::path& path) const;
    // Collect a ready write result. TakeTraceError stops tracing and consumes the error.
    [[nodiscard]] bool PollTraceError();
    [[nodiscard]] bool TakeTraceError();
private:
    struct Comparison;
    void UpdateCallbacks();
    void TraceWrite(std::string_view text);
    void SubmitTraceBatch();
    bool FinishTraceBatch(bool wait);
    static void OnInstruction(void* context, const CProcessor* processor, std::uint16_t address, std::uint16_t opcode);
    static void OnInterrupt(void* context, const CProcessor* processor, std::uint16_t oldPc, std::uint16_t vector);
    static void OnFloppyTrace(void* context, std::wstring_view text);
    CMotherboard& board_;
    std::unique_ptr<Comparison> comparison_;
    BreakpointSet cpu_, ppu_;
    InstructionHistory history_;
    bool historyEnabled_ = false;
    std::optional<DebugLogFile> trace_;
    std::string traceBatch_;
    std::string traceWork_;
    std::unique_ptr<FrameWorker> traceWriter_;
    bool tracePending_ = false;
    bool inFrame_ = false;
    std::filesystem::path tracePath_;
    std::uint16_t traceMask_ = 0;
    bool traceError_ = false;
    std::vector<DisasmSubtitleItem> subtitles_;
    std::filesystem::path subtitlePath_;
    bool subtitlesLoaded_ = false;
};
}
