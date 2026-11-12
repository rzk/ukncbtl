/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

// An in-memory INI document. Text decoding and file I/O belong to the caller.
// Section and key matching are ASCII case-insensitive; other characters are exact.
class IniDocument
{
public:
    explicit IniDocument(std::wstring_view text = {});
    [[nodiscard]] std::optional<std::wstring> Get(std::wstring_view section, std::wstring_view key) const;
    // Reject embedded NULs, line breaks and INI delimiters in names. Leading
    // and trailing spaces in values are rejected because INI reads trim them.
    [[nodiscard]] bool Set(std::wstring_view section, std::wstring_view key, std::wstring_view value);
    [[nodiscard]] std::wstring Serialize() const;

private:
    struct Line { std::wstring text; std::wstring ending; };
    std::vector<Line> lines_;
    std::wstring defaultEnding_ = L"\n";
};
