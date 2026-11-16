/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "PreviewSettings.h"
#include "util/ScreenScale.h"
#include "util/SettingsCodec.h"
#include <optional>
#include <string>
#include <utility>

namespace
{
constexpr std::wstring_view section = L"SDLPreview";

std::expected<std::optional<IniFile>, IniFileError> Read(const std::filesystem::path& path)
{
    if (path.empty()) return std::unexpected(IniFileError::open_failed);
    std::error_code error;
    const bool exists = std::filesystem::exists(path, error);
    if (error) return std::unexpected(IniFileError::open_failed);
    if (!exists) return std::optional<IniFile>{};
    auto file = IniFile::Open(path);
    if (!file) return std::unexpected(file.error());
    return std::optional<IniFile>{std::move(*file)};
}

std::optional<std::uint32_t> Number(const IniDocument& doc, std::wstring_view key)
{
    const auto text = doc.Get(section, key);
    return text && text->size() < 64 ? settings_codec::DecodeDword(*text) : std::nullopt;
}
}

bool preview_settings::ValidWindowSize(int width, int height)
{
    return width >= 320 && width <= 7680 && height >= 240 && height <= 4320;
}

std::expected<preview_settings::Settings, IniFileError> preview_settings::Load(const std::filesystem::path& path)
{
    auto file = Read(path);
    if (!file) return std::unexpected(file.error());
    Settings settings;
    if (!*file) return settings;
    const auto& doc = (*file)->Document();
    const auto width = Number(doc, L"WindowWidth");
    const auto height = Number(doc, L"WindowHeight");
    if (width && height && *width <= 7680 && *height <= 4320 &&
        ValidWindowSize(static_cast<int>(*width), static_cast<int>(*height)))
    {
        settings.windowWidth = static_cast<int>(*width);
        settings.windowHeight = static_cast<int>(*height);
    }
    const auto mode = Number(doc, L"ScreenshotMode");
    if (mode && *mode < screen_scale::Modes().size()) settings.screenshotMode = static_cast<int>(*mode);
    const auto sound = Number(doc, L"Sound");
    if (sound && *sound <= 1) settings.sound = *sound != 0;
    return settings;
}

std::expected<void, IniFileError> preview_settings::Save(const std::filesystem::path& path, const Settings& settings)
{
    if (!ValidWindowSize(settings.windowWidth, settings.windowHeight) || settings.screenshotMode < 0 ||
        static_cast<std::size_t>(settings.screenshotMode) >= screen_scale::Modes().size())
        return std::unexpected(IniFileError::invalid_entry);
    auto file = Read(path);
    if (!file) return std::unexpected(file.error());
    if (!*file) file->emplace();
    auto& doc = (*file)->Document();
    if (!doc.Set(section, L"WindowWidth", std::to_wstring(settings.windowWidth)) ||
        !doc.Set(section, L"WindowHeight", std::to_wstring(settings.windowHeight)) ||
        !doc.Set(section, L"ScreenshotMode", std::to_wstring(settings.screenshotMode)) ||
        !doc.Set(section, L"Sound", settings.sound ? L"1" : L"0"))
        return std::unexpected(IniFileError::invalid_entry);
    return (*file)->Save(path);
}
