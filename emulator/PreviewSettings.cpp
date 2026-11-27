/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "PreviewSettings.h"
#include "FramePacing.h"
#include "util/ScreenScale.h"
#include "util/SettingsCodec.h"
#include <algorithm>
#include <optional>
#include <string>
#include <utility>

namespace
{
constexpr std::wstring_view section = L"SDLPreview";

std::optional<std::wstring> EncodeLayout(std::string_view layout)
{
    if (layout.size() > preview_settings::MaxUiLayoutBytes || layout.find('\0') != layout.npos) return std::nullopt;
    const auto bytes = std::span(reinterpret_cast<const std::uint8_t*>(layout.data()), layout.size());
    if (!ini_encoding::Decode(bytes)) return std::nullopt;
    constexpr wchar_t digits[] = L"0123456789abcdef";
    std::wstring result;
    result.reserve(layout.size() * 2);
    for (const auto ch : layout)
    { const auto byte = static_cast<unsigned char>(ch); result.push_back(digits[byte >> 4]); result.push_back(digits[byte & 15]); }
    return result;
}

std::optional<std::string> DecodeLayout(std::wstring_view text)
{
    if ((text.size() & 1) || text.size() > preview_settings::MaxUiLayoutBytes * 2) return std::nullopt;
    const auto digit = [](wchar_t ch) -> int {
        if (ch >= L'0' && ch <= L'9') return ch - L'0';
        if (ch >= L'a' && ch <= L'f') return ch - L'a' + 10;
        if (ch >= L'A' && ch <= L'F') return ch - L'A' + 10;
        return -1;
    };
    std::string result;
    result.reserve(text.size() / 2);
    for (std::size_t index = 0; index < text.size(); index += 2)
    {
        const int high = digit(text[index]), low = digit(text[index + 1]);
        if (high < 0 || low < 0 || (high == 0 && low == 0)) return std::nullopt;
        result.push_back(static_cast<char>(high * 16 + low));
    }
    const auto bytes = std::span(reinterpret_cast<const std::uint8_t*>(result.data()), result.size());
    if (!ini_encoding::Decode(bytes)) return std::nullopt;
    return result;
}

// Hexadecimal UTF-8 keeps INI delimiters, whitespace and newlines in paths
// unambiguous and independent of the host's native path encoding.
std::optional<std::wstring> EncodePath(const std::filesystem::path& path)
{
    try
    {
        const auto utf8 = path.u8string();
        if (utf8.size() > 32768) return std::nullopt;
        constexpr wchar_t digits[] = L"0123456789abcdef";
        std::wstring result;
        result.reserve(utf8.size() * 2);
        for (const auto value : utf8)
        { const auto byte = static_cast<unsigned char>(value); result.push_back(digits[byte >> 4]); result.push_back(digits[byte & 15]); }
        return result;
    }
    catch (const std::filesystem::filesystem_error&) { return std::nullopt; }
}

std::optional<std::filesystem::path> DecodePath(std::wstring_view text)
{
    if (text.size() % 2 || text.size() > 65536) return std::nullopt;
    const auto digit = [](wchar_t ch) -> int {
        if (ch >= L'0' && ch <= L'9') return ch - L'0';
        if (ch >= L'a' && ch <= L'f') return ch - L'a' + 10;
        if (ch >= L'A' && ch <= L'F') return ch - L'A' + 10;
        return -1;
    };
    std::u8string utf8;
    utf8.reserve(text.size() / 2);
    for (std::size_t i = 0; i < text.size(); i += 2)
    {
        const int hi = digit(text[i]), lo = digit(text[i + 1]);
        if (hi < 0 || lo < 0 || (hi == 0 && lo == 0)) return std::nullopt;
        utf8.push_back(static_cast<char8_t>(hi * 16 + lo));
    }
    const auto bytes = std::span(reinterpret_cast<const std::uint8_t*>(utf8.data()), utf8.size());
    if (!ini_encoding::Decode(bytes)) return std::nullopt;
    try { return std::filesystem::path(utf8); }
    catch (const std::filesystem::filesystem_error&) { return std::nullopt; }
}

std::filesystem::path ReadPath(const IniDocument& doc, std::wstring_view key)
{
    const auto text = doc.Get(section, key);
    return text ? DecodePath(*text).value_or(std::filesystem::path{}) : std::filesystem::path{};
}

bool WritePath(IniDocument& doc, std::wstring_view key, const std::filesystem::path& path)
{
    const auto encoded = EncodePath(path);
    return encoded && doc.Set(section, key, *encoded);
}

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
    return text && text->size() < 64 ? settings_codec::DecodeUnsignedDecimal(*text) : std::nullopt;
}

bool ReadOnlySetting(const IniDocument& doc, std::wstring_view key)
{
    if (!doc.Get(section, key)) return false; // Older settings did not have a mount mode.
    const auto value = Number(doc, key);
    return !value || *value != 0; // Malformed values must not enable writes.
}

using SavedBreakpoints = std::array<std::uint16_t, BreakpointSet::Capacity - 1>;
std::wstring BreakpointKey(std::wstring_view prefix, std::size_t index)
{ return std::wstring(prefix) + std::to_wstring(index); }

SavedBreakpoints ReadBreakpoints(const IniDocument& doc, std::wstring_view prefix)
{
    auto result = preview_settings::EmptyBreakpoints();
    std::size_t count = 0;
    for (std::size_t index = 0; index < result.size(); ++index)
    {
        const auto value = Number(doc, BreakpointKey(prefix, index));
        if (!value || *value >= BreakpointSet::NoBreakpoint || (*value & 1)) continue;
        const auto address = static_cast<std::uint16_t>(*value);
        if (std::find(result.begin(), result.begin() + count, address) == result.begin() + count)
            result[count++] = address;
    }
    std::sort(result.begin(), result.begin() + count);
    return result;
}

bool ValidBreakpoints(const SavedBreakpoints& values)
{
    for (std::size_t index = 0; index < values.size(); ++index)
    {
        const auto value = values[index];
        if (value == BreakpointSet::NoBreakpoint) continue;
        if ((value & 1) || std::find(values.begin(), values.begin() + index, value) != values.begin() + index)
            return false;
    }
    return true;
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
    const auto mouse = Number(doc, L"Mouse");
    if (mouse && *mouse <= 1) settings.mouse = *mouse != 0;
    const auto keyboard = Number(doc, L"Keyboard");
    if (keyboard && *keyboard <= 1) settings.keyboard = *keyboard != 0;
    const auto autostart = Number(doc, L"Autostart");
    if (autostart && *autostart <= 1) settings.autostart = *autostart != 0;
    const auto spriteViewer = Number(doc, L"SpriteViewer");
    if (spriteViewer && *spriteViewer <= 1) settings.spriteViewer = *spriteViewer != 0;
    const auto spriteAddress = Number(doc, L"SpriteAddress");
    if (spriteAddress && *spriteAddress <= 0xffff) settings.spriteAddress = static_cast<std::uint16_t>(*spriteAddress);
    const auto spriteWidth = Number(doc, L"SpriteWidth");
    if (spriteWidth && *spriteWidth >= 1 && *spriteWidth <= 32) settings.spriteWidth = static_cast<int>(*spriteWidth);
    const auto spriteMode = Number(doc, L"SpriteMode");
    if (spriteMode && *spriteMode <= 4) settings.spriteMode = static_cast<int>(*spriteMode);
    const auto debuggerVisible = Number(doc, L"DebuggerVisible");
    if (debuggerVisible && *debuggerVisible <= 1) settings.debuggerVisible = *debuggerVisible != 0;
    const auto consoleVisible = Number(doc, L"ConsoleVisible");
    if (consoleVisible && *consoleVisible <= 1) settings.consoleVisible = *consoleVisible != 0;
    const auto debuggerPpu = Number(doc, L"DebuggerPpu");
    if (debuggerPpu && *debuggerPpu <= 1) settings.debuggerPpu = *debuggerPpu != 0;
    const auto station = Number(doc, L"NetworkStation");
    if (station && *station <= 0xffff) settings.networkStation = static_cast<std::uint16_t>(*station);
    settings.cpuBreakpoints = ReadBreakpoints(doc, L"CpuBreakpoint");
    settings.ppuBreakpoints = ReadBreakpoints(doc, L"PpuBreakpoint");
    const auto restore = Number(doc, L"RestoreMedia");
    if (restore && *restore <= 1) settings.restoreMedia = *restore != 0;
    settings.systemRom = ReadPath(doc, L"SystemRomPath");
    settings.tapePath = ReadPath(doc, L"TapePath");
    const auto tapeVisible = Number(doc, L"TapeVisible");
    if (tapeVisible && *tapeVisible <= 1) settings.tapeVisible = *tapeVisible != 0;
    const auto mediaVisible = Number(doc, L"MediaVisible");
    if (mediaVisible && *mediaVisible <= 1) settings.mediaVisible = *mediaVisible != 0;
    for (std::size_t index = 0; index < settings.floppies.size(); ++index)
    {
        settings.floppies[index] = ReadPath(doc, L"FloppyPath" + std::to_wstring(index));
        settings.floppyReadOnly[index] = ReadOnlySetting(doc, L"FloppyReadOnly" + std::to_wstring(index));
    }
    for (std::size_t index = 0; index < settings.cartridges.size(); ++index)
    {
        settings.cartridges[index] = ReadPath(doc, L"CartridgePath" + std::to_wstring(index));
        settings.hardDisks[index] = ReadPath(doc, L"HardDiskPath" + std::to_wstring(index));
        settings.hardDiskReadOnly[index] = ReadOnlySetting(doc, L"HardDiskReadOnly" + std::to_wstring(index));
    }
    const auto memorySource = Number(doc, L"MemorySource");
    if (memorySource && *memorySource <= 6) settings.memorySource = static_cast<int>(*memorySource);
    const auto memoryBytes = Number(doc, L"MemoryBytes");
    if (memoryBytes && *memoryBytes <= 1) settings.memoryBytes = *memoryBytes != 0;
    const auto memoryHex = Number(doc, L"MemoryHex");
    if (memoryHex && *memoryHex <= 1) settings.memoryHex = *memoryHex != 0;
    const auto toolbar = Number(doc, L"Toolbar");
    if (toolbar && *toolbar <= 1) settings.toolbar = *toolbar != 0;
    const auto statusBar = Number(doc, L"StatusBar");
    if (statusBar && *statusBar <= 1) settings.statusBar = *statusBar != 0;
    const auto traceMask = Number(doc, L"TraceMask");
    if (traceMask && *traceMask && !(*traceMask & ~03700u)) settings.traceMask = static_cast<std::uint16_t>(*traceMask);
    const auto keyboardLegend = Number(doc, L"KeyboardLegend");
    if (keyboardLegend && *keyboardLegend <= 3) settings.keyboardLegend = static_cast<int>(*keyboardLegend);
    const auto keyboardScale = Number(doc, L"KeyboardScale");
    if (keyboardScale && *keyboardScale >= 75 && *keyboardScale <= 175) settings.keyboardScale = static_cast<int>(*keyboardScale);
    const auto uiTheme = Number(doc, L"UiTheme");
    if (uiTheme && *uiTheme <= 2) settings.uiTheme = static_cast<int>(*uiTheme);
    const auto uiScale = Number(doc, L"UiScale");
    if (uiScale && *uiScale >= 75 && *uiScale <= 200) settings.uiScale = static_cast<int>(*uiScale);
    if (const auto layout = doc.Get(section, L"UiLayout")) settings.uiLayout = DecodeLayout(*layout).value_or(std::string{});
    return settings;
}

std::expected<void, IniFileError> preview_settings::Save(const std::filesystem::path& path, const Settings& settings)
{
    if (!ValidWindowSize(settings.windowWidth, settings.windowHeight) || settings.screenshotMode < 0 ||
        static_cast<std::size_t>(settings.screenshotMode) >= screen_scale::Modes().size() || settings.displayMode < 0 ||
        static_cast<std::size_t>(settings.displayMode) >= screen_scale::Modes().size() ||
        settings.paletteMode < 0 || settings.paletteMode > 2 || !frame_pacing::IsSupportedSpeed(settings.speed) ||
        settings.volume < 0 || settings.volume > 100 || settings.spriteWidth < 1 || settings.spriteWidth > 32 ||
        settings.spriteMode < 0 || settings.spriteMode > 4 || settings.memorySource < 0 || settings.memorySource > 6 ||
        !settings.traceMask || (settings.traceMask & ~03700u) ||
        settings.keyboardLegend < 0 || settings.keyboardLegend > 3 || settings.keyboardScale < 75 || settings.keyboardScale > 175 ||
        settings.uiTheme < 0 || settings.uiTheme > 2 || settings.uiScale < 75 || settings.uiScale > 200 ||
        !ValidBreakpoints(settings.cpuBreakpoints) || !ValidBreakpoints(settings.ppuBreakpoints))
        return std::unexpected(IniFileError::invalid_entry);
    const auto uiLayout = EncodeLayout(settings.uiLayout);
    if (!uiLayout) return std::unexpected(IniFileError::invalid_entry);
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
        !doc.Set(section, L"Volume", std::to_wstring(settings.volume)) ||
        !doc.Set(section, L"Mouse", settings.mouse ? L"1" : L"0") ||
        !doc.Set(section, L"Keyboard", settings.keyboard ? L"1" : L"0") ||
        !doc.Set(section, L"Autostart", settings.autostart ? L"1" : L"0") ||
        !doc.Set(section, L"SpriteViewer", settings.spriteViewer ? L"1" : L"0") ||
        !doc.Set(section, L"SpriteAddress", std::to_wstring(settings.spriteAddress)) ||
        !doc.Set(section, L"SpriteWidth", std::to_wstring(settings.spriteWidth)) ||
        !doc.Set(section, L"SpriteMode", std::to_wstring(settings.spriteMode)) ||
        !doc.Set(section, L"DebuggerVisible", settings.debuggerVisible ? L"1" : L"0") ||
        !doc.Set(section, L"ConsoleVisible", settings.consoleVisible ? L"1" : L"0") ||
        !doc.Set(section, L"DebuggerPpu", settings.debuggerPpu ? L"1" : L"0") ||
        !doc.Set(section, L"NetworkStation", std::to_wstring(settings.networkStation)) ||
        !doc.Set(section, L"MemorySource", std::to_wstring(settings.memorySource)) ||
        !doc.Set(section, L"MemoryBytes", settings.memoryBytes ? L"1" : L"0") ||
        !doc.Set(section, L"MemoryHex", settings.memoryHex ? L"1" : L"0") ||
        !doc.Set(section, L"Toolbar", settings.toolbar ? L"1" : L"0") ||
        !doc.Set(section, L"StatusBar", settings.statusBar ? L"1" : L"0") ||
        !doc.Set(section, L"TraceMask", std::to_wstring(settings.traceMask)) ||
        !doc.Set(section, L"KeyboardLegend", std::to_wstring(settings.keyboardLegend)) ||
        !doc.Set(section, L"KeyboardScale", std::to_wstring(settings.keyboardScale)) ||
        !doc.Set(section, L"UiTheme", std::to_wstring(settings.uiTheme)) ||
        !doc.Set(section, L"UiScale", std::to_wstring(settings.uiScale)) ||
        !doc.Set(section, L"UiLayout", *uiLayout))
        return std::unexpected(IniFileError::invalid_entry);
    for (std::size_t index = 0; index < settings.cpuBreakpoints.size(); ++index)
        if (!doc.Set(section, BreakpointKey(L"CpuBreakpoint", index), std::to_wstring(settings.cpuBreakpoints[index])) ||
            !doc.Set(section, BreakpointKey(L"PpuBreakpoint", index), std::to_wstring(settings.ppuBreakpoints[index])))
            return std::unexpected(IniFileError::invalid_entry);
    if (!doc.Set(section, L"RestoreMedia", settings.restoreMedia ? L"1" : L"0") ||
        !WritePath(doc, L"SystemRomPath", settings.systemRom) || !WritePath(doc, L"TapePath", settings.tapePath) ||
        !doc.Set(section, L"TapeVisible", settings.tapeVisible ? L"1" : L"0") ||
        !doc.Set(section, L"MediaVisible", settings.mediaVisible ? L"1" : L"0")) return std::unexpected(IniFileError::invalid_entry);
    for (std::size_t index = 0; index < settings.floppies.size(); ++index)
        if (!WritePath(doc, L"FloppyPath" + std::to_wstring(index), settings.floppies[index]) ||
            !doc.Set(section, L"FloppyReadOnly" + std::to_wstring(index), settings.floppyReadOnly[index] ? L"1" : L"0"))
            return std::unexpected(IniFileError::invalid_entry);
    for (std::size_t index = 0; index < settings.cartridges.size(); ++index)
        if (!WritePath(doc, L"CartridgePath" + std::to_wstring(index), settings.cartridges[index]) ||
            !WritePath(doc, L"HardDiskPath" + std::to_wstring(index), settings.hardDisks[index]) ||
            !doc.Set(section, L"HardDiskReadOnly" + std::to_wstring(index), settings.hardDiskReadOnly[index] ? L"1" : L"0"))
            return std::unexpected(IniFileError::invalid_entry);
    return (*file)->Save(path);
}
