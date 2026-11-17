/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once
#include "PreviewDebugger.h"
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace preview_console
{
enum class Action { none, run, pause, saveMemory, saveDisplayList };
struct MemoryRange { std::uint16_t start, end; };
// Commands and output are UTF-8; addresses and words use octal.
class Console
{
public:
    [[nodiscard]] Action Submit(std::string_view command, preview_debugger::Session& debugger,
        preview_debugger::Processor& processor, bool running);
    [[nodiscard]] std::string_view Navigate(int direction);
    [[nodiscard]] const std::string& Log() const { return log_; }
    [[nodiscard]] bool Save(const std::filesystem::path& path) const;
    [[nodiscard]] std::optional<MemoryRange> TakeMemoryRange();
private:
    void Append(std::string_view text);
    std::string log_;
    std::vector<std::string> commands_;
    int navigation_ = -1;
    std::optional<MemoryRange> memoryRange_;
};
}
