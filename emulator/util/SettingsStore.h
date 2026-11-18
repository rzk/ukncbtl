/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once

#include "IniFile.h"
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
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
    // Malformed values are treated as missing. Colors use the existing packed
    // 0x00BBGGRR representation. Binary reads require an exact-size match.
    [[nodiscard]] std::expected<std::optional<std::uint32_t>, IniFileError> GetDword(std::wstring_view key) const;
    [[nodiscard]] std::expected<void, IniFileError> SetDword(std::wstring_view key, std::uint32_t value) const;
    [[nodiscard]] std::expected<std::optional<std::uint32_t>, IniFileError> GetColor(std::wstring_view key) const;
    [[nodiscard]] std::expected<void, IniFileError> SetColor(std::wstring_view key, std::uint32_t value) const;
    [[nodiscard]] std::expected<bool, IniFileError> GetBinary(std::wstring_view key, std::span<std::uint8_t> output) const;
    [[nodiscard]] std::expected<void, IniFileError> SetBinary(std::wstring_view key, std::span<const std::uint8_t> value) const;

private:
    [[nodiscard]] std::expected<std::optional<IniFile>, IniFileError> Read() const;
    std::filesystem::path path_;
};
