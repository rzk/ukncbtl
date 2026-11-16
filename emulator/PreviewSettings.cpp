/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "PreviewSettings.h"
#include "FramePacing.h"
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
    const auto display = Number(doc, L"DisplayMode");
    if (display && *display < screen_scale::Modes().size()) settings.displayMode = static_cast<int>(*display);
    const auto palette = Number(doc, L"PaletteMode");
    if (palette && *palette <= 2) settings.paletteMode = static_cast<int>(*palette);
    const auto speed = Number(doc, L"Speed");
    if (speed && *speed <= 0xffff && frame_pacing::IsSupportedSpeed(static_cast<std::uint16_t>(*speed)))
        settings.speed = static_cast<std::uint16_t>(*speed);
    const auto ay = Number(doc, L"SoundAY");
    if (ay && *ay <= 1) settings.soundAY = *ay != 0;
    const auto covox = Number(doc, L"SoundCovox");
    if (covox && *covox <= 1) settings.soundCovox = *covox != 0;
    const auto volume = Number(doc, L"Volume");
    if (volume && *volume <= 100) settings.volume = static_cast<int>(*volume);
    return settings;
}

std::expected<void, IniFileError> preview_settings::Save(const std::filesystem::path& path, const Settings& settings)
{
    if (!ValidWindowSize(settings.windowWidth, settings.windowHeight) || settings.screenshotMode < 0 ||
        static_cast<std::size_t>(settings.screenshotMode) >= screen_scale::Modes().size() || settings.displayMode < 0 ||
        static_cast<std::size_t>(settings.displayMode) >= screen_scale::Modes().size() ||
        settings.paletteMode < 0 || settings.paletteMode > 2 || !frame_pacing::IsSupportedSpeed(settings.speed) ||
        settings.volume < 0 || settings.volume > 100)
        return std::unexpected(IniFileError::invalid_entry);
    auto file = Read(path);
    if (!file) return std::unexpected(file.error());
    if (!*file) file->emplace();
    auto& doc = (*file)->Document();
    if (!doc.Set(section, L"WindowWidth", std::to_wstring(settings.windowWidth)) ||
        !doc.Set(section, L"WindowHeight", std::to_wstring(settings.windowHeight)) ||
        !doc.Set(section, L"ScreenshotMode", std::to_wstring(settings.screenshotMode)) ||
        !doc.Set(section, L"Sound", settings.sound ? L"1" : L"0") ||
        !doc.Set(section, L"DisplayMode", std::to_wstring(settings.displayMode)) ||
        !doc.Set(section, L"PaletteMode", std::to_wstring(settings.paletteMode)) ||
        !doc.Set(section, L"Speed", std::to_wstring(settings.speed)) ||
        !doc.Set(section, L"SoundAY", settings.soundAY ? L"1" : L"0") ||
        !doc.Set(section, L"SoundCovox", settings.soundCovox ? L"1" : L"0") ||
        !doc.Set(section, L"Volume", std::to_wstring(settings.volume)))
        return std::unexpected(IniFileError::invalid_entry);
    return (*file)->Save(path);
}
