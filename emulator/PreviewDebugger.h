/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once
#include "BreakpointSet.h"
#include "emubase/Disasm.h"
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

class CMotherboard;
class CProcessor;
namespace preview_debugger
{
enum class Processor { cpu, ppu };
struct Line { std::uint16_t address, word; DisassembledInstruction instruction; };
[[nodiscard]] std::optional<std::uint16_t> ParseAddress(std::string_view text);

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
    [[nodiscard]] std::vector<Line> Disassemble(Processor which, std::uint16_t address, unsigned count) const;
    void PrepareRun();
    void Stop();
    void Step();
    // True means a temporary breakpoint was installed and the machine should run.
    // False means step-over fell back to a single debug tick.
    [[nodiscard]] bool StepOver(Processor which);
    [[nodiscard]] bool RunFrame();
private:
    CMotherboard& board_;
    BreakpointSet cpu_, ppu_;
};
}
