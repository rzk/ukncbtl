/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once

#include "IniFile.h"
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

// Reads the file on each operation so external edits are visible. Callers must
// coordinate concurrent writers. Empty values are treated as missing on read,
// matching the existing application settings API.
class SettingsStore
{
public:
    explicit SettingsStore(std::filesystem::path path);
    [[nodiscard]] std::expected<std::optional<std::wstring>, IniFileError> Get(std::wstring_view key) const;
    [[nodiscard]] std::expected<void, IniFileError> Set(std::wstring_view key, std::wstring_view value) const;
    [[nodiscard]] std::expected<void, IniFileError> Remove(std::wstring_view key) const;

private:
    [[nodiscard]] std::expected<std::optional<IniFile>, IniFileError> Read() const;
    std::filesystem::path path_;
};
