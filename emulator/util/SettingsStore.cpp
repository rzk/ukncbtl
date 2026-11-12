/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "SettingsStore.h"
#include "SettingsCodec.h"
#include <utility>

namespace
{
constexpr std::wstring_view Section = L"UKNCBTL";
}

SettingsStore::SettingsStore(std::filesystem::path path) : path_(std::move(path)) {}

std::expected<std::optional<IniFile>, IniFileError> SettingsStore::Read() const
{
    std::error_code error;
    const bool exists = std::filesystem::exists(path_, error);
    if (error) return std::unexpected(IniFileError::open_failed);
    if (!exists) return std::optional<IniFile>{};
    auto file = IniFile::Open(path_);
    if (!file) return std::unexpected(file.error());
    return std::optional<IniFile>{std::move(*file)};
}

std::expected<std::optional<std::wstring>, IniFileError> SettingsStore::Get(std::wstring_view key) const
{
    auto file = Read();
    if (!file) return std::unexpected(file.error());
    if (!*file) return std::optional<std::wstring>{};
    auto value = (*file)->Document().Get(Section, key);
    if (value && value->empty()) value.reset();
    return value;
}

std::expected<void, IniFileError> SettingsStore::Set(std::wstring_view key, std::wstring_view value) const
{
    auto file = Read();
    if (!file) return std::unexpected(file.error());
    if (!*file) file->emplace();
    if (!(*file)->Document().Set(Section, key, value)) return std::unexpected(IniFileError::invalid_entry);
    return (*file)->Save(path_);
}

std::expected<void, IniFileError> SettingsStore::Remove(std::wstring_view key) const
{
    auto file = Read();
    if (!file) return std::unexpected(file.error());
    if (!*file || !(*file)->Document().Remove(Section, key)) return {};
    return (*file)->Save(path_);
}

std::expected<std::optional<std::uint32_t>, IniFileError> SettingsStore::GetDword(std::wstring_view key) const
{
    const auto text = Get(key);
    if (!text) return std::unexpected(text.error());
    return *text && (*text)->size() < 64 ? settings_codec::DecodeDword(**text) : std::nullopt;
}

std::expected<void, IniFileError> SettingsStore::SetDword(std::wstring_view key, std::uint32_t value) const
{
    return Set(key, std::to_wstring(value));
}

std::expected<std::optional<std::uint32_t>, IniFileError> SettingsStore::GetColor(std::wstring_view key) const
{
    const auto text = Get(key);
    if (!text) return std::unexpected(text.error());
    return *text && (*text)->size() < 64 ? settings_codec::DecodeColor(**text) : std::nullopt;
}

std::expected<void, IniFileError> SettingsStore::SetColor(std::wstring_view key, std::uint32_t value) const
{
    return Set(key, settings_codec::EncodeColor(value));
}

std::expected<bool, IniFileError> SettingsStore::GetBinary(std::wstring_view key, std::span<std::uint8_t> output) const
{
    const auto text = Get(key);
    if (!text) return std::unexpected(text.error());
    return *text && settings_codec::DecodeBinary(**text, output);
}

std::expected<void, IniFileError> SettingsStore::SetBinary(std::wstring_view key, std::span<const std::uint8_t> value) const
{
    // Hex encoding doubles the size. An encoded value larger than the file
    // limit cannot be saved, so reject it before allocating the string.
    if (value.size() > IniFile::MaxBytes / 2) return std::unexpected(IniFileError::too_large);
    return Set(key, settings_codec::EncodeBinary(value));
}
