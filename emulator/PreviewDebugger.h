/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once
#include "BreakpointSet.h"
#include "InstructionHistory.h"
#include "SubtitleFile.h"
#include "util/DebugLogFile.h"
#include "emubase/Disasm.h"
#include <cstdint>
#include <optional>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

class CMotherboard;
class CProcessor;
namespace preview_debugger
{
enum class Processor { cpu, ppu };
enum class DumpError { invalid_range, file_error };
// These match the legacy CPU/PPU instruction and interrupt trace masks.
inline constexpr std::uint16_t TraceInstructionsPpu = 0200, TraceInstructionsCpu = 0400;
inline constexpr std::uint16_t TraceInterruptsPpu = 01000, TraceInterruptsCpu = 02000;
inline constexpr std::uint16_t TraceAll = 03600;
struct Line
{
    std::uint16_t address, word;
    DisassembledInstruction instruction;
    std::string blockComment, comment;
    bool data = false;
};
[[nodiscard]] std::optional<std::uint16_t> ParseAddress(std::string_view text);
[[nodiscard]] std::optional<std::uint16_t> ParseWord(std::string_view text);
[[nodiscard]] std::optional<std::string> ToUtf8(std::wstring_view text);

// Owns breakpoint storage borrowed by the board. The board must outlive it.
class Session
{
public:
    explicit Session(CMotherboard& board) : board_(board) {}
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    [[nodiscard]] CProcessor& GetProcessor(Processor which) const;
    [[nodiscard]] BreakpointSet& Breakpoints(Processor which);
    [[nodiscard]] std::uint16_t ReadWord(Processor which, std::uint16_t address, bool instruction) const;
    [[nodiscard]] std::uint16_t ReadPort(Processor which, std::uint16_t address) const;
    // Register 0..7 selects R0..PC; index 8 selects PSW.
    [[nodiscard]] bool WriteRegister(Processor which, int index, std::uint16_t value);
    // Only aligned RAM words are writable; ROM and I/O are never touched.
    [[nodiscard]] bool WriteMemory(Processor which, std::uint16_t address, std::uint16_t value);
    // The end address is exclusive; endpoints are rounded to whole words.
    [[nodiscard]] std::expected<void, DumpError> SaveMemory(const std::filesystem::path& path,
        Processor which, std::uint16_t start, std::uint16_t end) const;
    [[nodiscard]] std::vector<Line> Disassemble(Processor which, std::uint16_t address, unsigned count,
        bool useSubtitles = false) const;
    [[nodiscard]] std::expected<void, subtitles::FileError> LoadSubtitles(const std::filesystem::path& path);
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
    [[nodiscard]] bool TakeTraceError();
private:
    void UpdateCallbacks();
    void TraceWrite(std::string_view text);
    static void OnInstruction(void* context, const CProcessor* processor, std::uint16_t address, std::uint16_t opcode);
    static void OnInterrupt(void* context, const CProcessor* processor, std::uint16_t oldPc, std::uint16_t vector);
    CMotherboard& board_;
    BreakpointSet cpu_, ppu_;
    InstructionHistory history_;
    bool historyEnabled_ = false;
    std::optional<DebugLogFile> trace_;
    std::filesystem::path tracePath_;
    std::uint16_t traceMask_ = 0;
    bool traceError_ = false;
    std::vector<DisasmSubtitleItem> subtitles_;
    std::filesystem::path subtitlePath_;
    bool subtitlesLoaded_ = false;
};
}
