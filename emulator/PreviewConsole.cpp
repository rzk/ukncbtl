/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "PreviewConsole.h"
#include "emubase/Processor.h"
#include <algorithm>
#include <format>
#include <fstream>
#include <sstream>
#include <utility>
#include <vector>

namespace
{
constexpr std::size_t maxLogBytes = 65536, maxCommands = 32;
using preview_debugger::ParseAddress;
using preview_debugger::ParseWord;
using preview_console::Action;

std::vector<std::string> Tokens(std::string_view input)
{
    std::string normalized;
    for (const char ch : input) normalized.push_back(ch == '=' ? ' ' : ch);
    std::istringstream stream(normalized);
    std::vector<std::string> result;
    for (std::string token; stream >> token;) result.push_back(std::move(token));
    if (!result.empty())
    {
        auto& first = result[0];
        std::size_t prefix = 0;
        if (first.size() > 2 && (first.starts_with("bc") || first.starts_with("fc")) &&
            first[2] >= '0' && first[2] <= '9') prefix = 2;
        else if (first.size() > 1 && (first[0] == 'b' || first[0] == 'd' || first[0] == 'D' || first[0] == 'm' || first[0] == 'g' || first[0] == 'u' || first[0] == 't') &&
            first[1] >= '0' && first[1] <= '9') prefix = 1;
        if (prefix)
        {
            auto tail = first.substr(prefix);
            first.resize(prefix);
            result.insert(result.begin() + 1, std::move(tail));
        }
    }
    return result;
}
}

void preview_console::Console::Append(std::string_view text)
{
    log_.append(text);
    while (log_.size() > maxLogBytes)
    {
        const auto newline = log_.find('\n');
        if (newline == std::string::npos)
        {
            auto cut = log_.size() - maxLogBytes;
            while (cut < log_.size() && (static_cast<unsigned char>(log_[cut]) & 0xc0) == 0x80) ++cut;
            log_.erase(0, cut);
            break;
        }
        log_.erase(0, newline + 1);
    }
}

std::string_view preview_console::Console::Navigate(int direction)
{
    if (direction < 0 && navigation_ + 1 < static_cast<int>(commands_.size())) ++navigation_;
    else if (direction > 0 && navigation_ >= 0) --navigation_;
    return navigation_ < 0 ? std::string_view{} : std::string_view(commands_[commands_.size() - 1 - navigation_]);
}

bool preview_console::Console::Save(const std::filesystem::path& path) const
{
    if (path.empty()) return false;
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) return false;
    file.write(log_.data(), static_cast<std::streamsize>(log_.size()));
    file.flush();
    const bool written = static_cast<bool>(file);
    file.close();
    return written && static_cast<bool>(file);
}

std::optional<preview_console::MemoryRange> preview_console::Console::TakeMemoryRange()
{
    return std::exchange(memoryRange_, std::nullopt);
}

std::optional<std::uint16_t> preview_console::Console::TakeTraceMask()
{
    return std::exchange(traceMask_, std::nullopt);
}

preview_console::Action preview_console::Console::Submit(std::string_view command,
    preview_debugger::Session& debugger, preview_debugger::Processor& processor, bool running)
{
    memoryRange_.reset();
    traceMask_.reset();
    if (command.size() > 4096) { Append("Command is too long.\n"); return Action::none; }
    const auto tokens = Tokens(command);
    if (tokens.empty()) return Action::none;
    std::erase(commands_, std::string(command));
    commands_.emplace_back(command);
    if (commands_.size() > maxCommands) commands_.erase(commands_.begin());
    navigation_ = -1;
    Append(std::format("> {}\n", command));
    const auto& op = tokens[0];
    const auto say = [&](std::string_view message) { Append(message); Append("\n"); };
    const auto fail = [&] { say("Invalid command or octal argument. Enter h for help."); return Action::none; };
    const auto paused = [&] { if (running) { say("Pause the machine first."); return false; } return true; };
    auto& cpu = debugger.GetProcessor(processor);
    if ((op == "h" || op == "help") && tokens.size() == 1)
    {
        say("p: CPU/PPU  r: registers  r0 [value] / rps [value]: register\n"
            "m [address [value]]: memory  mr0..mr7: memory at register\n"
            "d [address]: disassemble  D [address]: short disassembly\n"
            "b [address]: list/add breakpoint  bc [address]: clear breakpoint(s)\n"
            "s: step  so: step over  g [address]: run/run to  pause: pause  c: clear log\n"
            "u [start end]: save memory  udl: save display list\n"
            "t [mask]: start/stop trace or set octal mask  tc: clear trace\n"
            "Trace masks: PPU 200, CPU 400, PPU interrupt 1000, CPU interrupt 2000\n"
            "fc high low: convert two octal FIS floating-point words");
    }
    else if (op == "c" && tokens.size() == 1) log_.clear();
    else if (op == "tc" && tokens.size() == 1)
    { say(debugger.ClearTrace() ? "Trace cleared." : "Trace is not active or could not be cleared."); }
    else if (op == "t" && tokens.size() <= 2)
    {
        if (tokens.size() == 1 && debugger.TracePath())
        { debugger.StopTrace(); say("Trace stopped."); }
        else
        {
            const auto mask = tokens.size() == 1 ? std::optional<std::uint16_t>{preview_debugger::TraceAll} : ParseWord(tokens[1]);
            if (!mask || (*mask & ~preview_debugger::TraceAll)) return fail();
            if (*mask == 0) { debugger.StopTrace(); say("Trace stopped."); }
            else if (debugger.TracePath())
            { (void)debugger.SetTraceMask(*mask); say("Trace mask updated."); }
            else
            { traceMask_ = *mask; say("Choose a trace file."); return Action::startTrace; }
        }
    }
    else if (op == "udl" && tokens.size() == 1)
    { say("Choose a file for the display list."); return Action::saveDisplayList; }
    else if (op == "u" && (tokens.size() == 1 || tokens.size() == 3))
    {
        const auto start = tokens.size() == 1 ? std::optional<std::uint16_t>{0} : ParseWord(tokens[1]);
        const auto end = tokens.size() == 1 ? std::optional<std::uint16_t>{0xffff} : ParseWord(tokens[2]);
        if (!start || !end || *end <= *start) return fail();
        memoryRange_ = MemoryRange{*start, *end};
        say("Choose a file for the memory dump.");
        return Action::saveMemory;
    }
    else if (op == "p" && tokens.size() == 1)
    {
        processor = processor == preview_debugger::Processor::cpu ? preview_debugger::Processor::ppu : preview_debugger::Processor::cpu;
        say(processor == preview_debugger::Processor::cpu ? "CPU selected." : "PPU selected.");
    }
    else if (op == "r" && tokens.size() == 1)
    {
        for (int index = 0; index < 8; ++index) Append(std::format("R{}={:06o}{}", index,
            cpu.GetReg(index), index == 7 ? "\n" : "  "));
        Append(std::format("PSW={:06o}  PC'={:06o}  PSW'={:06o}\n", cpu.GetPSW(), cpu.GetCPC(), cpu.GetCPSW()));
    }
    else if ((op == "rps" || (op.size() == 2 && op[0] == 'r' && op[1] >= '0' && op[1] <= '7')) && tokens.size() <= 2)
    {
        const int index = op == "rps" ? 8 : op[1] - '0';
        if (tokens.size() == 1) Append(std::format("{}={:06o}\n", op, index == 8 ? cpu.GetPSW() : cpu.GetReg(index)));
        else
        {
            const auto value = ParseWord(tokens[1]);
            if (!value) return fail();
            if (!paused()) return Action::none;
            (void)debugger.WriteRegister(processor, index, *value);
            say("Register updated.");
        }
    }
    else if (op == "fc" && tokens.size() == 3)
    {
        const auto high = ParseWord(tokens[1]);
        const auto low = ParseWord(tokens[2]);
        if (!high || !low) return fail();
        const auto mantissa = ((static_cast<std::uint32_t>(*high) << 16 | *low) & 0x7fffffu) | 0x800000u;
        const int exponent = (*high >> 7) & 0xff;
        const auto value = preview_debugger::ToUtf8(Disasm_FloatToString(*high, *low)).value_or("?");
        Append(std::format("exp: {}, mant: 0x{:x}\n{}\n", exponent == 0 ? 0 : exponent - 128, mantissa, value));
    }
    else if ((op == "m" || op == "d" || op == "D" || op.starts_with("mr")) &&
        tokens.size() <= (op == "m" ? 3u : op.starts_with("mr") ? 1u : 2u))
    {
        const bool registerMemory = op.starts_with("mr");
        if (registerMemory && (op.size() != 3 || op[2] < '0' || op[2] > '7')) return fail();
        const auto address = registerMemory ? std::optional{cpu.GetReg(op[2] - '0')} :
            tokens.size() >= 2 ? ParseAddress(tokens[1]) : std::optional{cpu.GetPC()};
        if (!address || (*address & 1)) return fail();
        if (op == "m" && tokens.size() == 3)
        {
            const auto value = ParseWord(tokens[2]);
            if (!value) return fail();
            if (!paused()) return Action::none;
            say(debugger.WriteMemory(processor, *address, *value) ? "RAM updated." : "Address is not writable RAM.");
        }
        else if (op == "m" || registerMemory)
            for (int row = 0; row < 8; ++row)
            {
                const auto at = static_cast<std::uint16_t>(*address + row * 2);
                Append(std::format("{:06o}  {:06o}\n", at, debugger.ReadWord(processor, at, false)));
            }
        else for (const auto& line : debugger.Disassemble(processor, *address, 8, true))
        {
            if (!line.blockComment.empty()) { Append(line.blockComment); Append("\n"); }
            const auto mnemonic = preview_debugger::ToUtf8(line.instruction.mnemonic).value_or("?");
            const auto argument = preview_debugger::ToUtf8(line.instruction.argument).value_or("?");
            if (op == "D") Append(std::format("{:06o}  {} {}\n", line.address, mnemonic, argument));
            else Append(std::format("{:06o}  {:06o}  {} {} {}\n", line.address, line.word, mnemonic, argument, line.comment));
        }
    }
    else if ((op == "b" || op == "bc") && tokens.size() <= 2)
    {
        auto& points = debugger.Breakpoints(processor);
        if (tokens.size() == 1 && op == "b")
        {
            if (points.Addresses().empty()) say("No breakpoints.");
            for (const auto address : points.Addresses()) Append(std::format("{:06o}\n", address));
        }
        else
        {
            if (!paused()) return Action::none;
            if (tokens.size() == 1) { points.Clear(); say("Breakpoints cleared."); }
            else
            {
                const auto address = ParseAddress(tokens[1]);
                if (!address) return fail();
                const bool changed = op == "b" ? points.Add(*address) : points.Remove(*address);
                say(changed ? "Breakpoint updated." : "Breakpoint could not be updated.");
            }
        }
    }
    else if (op == "s" && tokens.size() == 1)
    { if (paused()) { debugger.Step(); say("Stepped both processors."); } }
    else if (op == "so" && tokens.size() == 1)
    {
        if (paused()) { if (debugger.StepOver(processor)) { say("Running to next instruction."); return Action::run; } say("Stepped both processors."); }
    }
    else if (op == "g" && tokens.size() <= 2)
    {
        if (!paused()) return Action::none;
        if (tokens.size() == 2)
        {
            const auto address = ParseAddress(tokens[1]);
            if (!address || !debugger.RunTo(processor, *address)) return fail();
        }
        say("Running."); return Action::run;
    }
    else if (op == "pause" && tokens.size() == 1)
    { if (running) { say("Paused."); return Action::pause; } say("Already paused."); }
    else return fail();
    return Action::none;
}
