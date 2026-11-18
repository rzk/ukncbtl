/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once

#include <filesystem>
#include <fstream>
#include <string_view>

// A lazily opened binary log. The first open after Close truncates the file.
// Appends are flushed so another process can read completed log entries.
class DebugLogFile
{
public:
    explicit DebugLogFile(std::filesystem::path path);
    [[nodiscard]] bool Open();
    [[nodiscard]] bool Clear();
    [[nodiscard]] bool Append(std::string_view bytes);
    // Encodes complete Unicode text as UTF-8; invalid text is not appended.
    [[nodiscard]] bool AppendText(std::wstring_view text);
    void Close();

private:
    std::filesystem::path path_;
    std::ofstream file_;
};
