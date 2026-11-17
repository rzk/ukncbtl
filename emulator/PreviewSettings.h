/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once

#include "util/IniFile.h"
#include "BreakpointSet.h"
#include <array>
#include <expected>
#include <filesystem>
#include <cstdint>
#include <string>

namespace preview_settings
{
[[nodiscard]] constexpr std::array<std::uint16_t, BreakpointSet::Capacity - 1> EmptyBreakpoints()
{
    std::array<std::uint16_t, BreakpointSet::Capacity - 1> result{};
    result.fill(BreakpointSet::NoBreakpoint);
    return result;
}
struct Settings
{
    int windowWidth = 960;
    int windowHeight = 720;
    int screenshotMode = 1;
    bool sound = true;
    int displayMode = 1;
    int paletteMode = 0;
    std::uint16_t speed = 1;
    bool soundAY = false;
    bool soundCovox = false;
    int volume = 100;
    bool mouse = false;
    bool keyboard = false;
    bool autostart = true;
    bool spriteViewer = false;
    std::uint16_t spriteAddress = 0;
    int spriteWidth = 2;
    int spriteMode = 0;
    bool debuggerVisible = false;
    bool consoleVisible = false;
    bool debuggerPpu = false;
    std::uint16_t networkStation = 0;
    std::array<std::uint16_t, BreakpointSet::Capacity - 1> cpuBreakpoints = EmptyBreakpoints();
    std::array<std::uint16_t, BreakpointSet::Capacity - 1> ppuBreakpoints = EmptyBreakpoints();
    bool restoreMedia = false;
    std::filesystem::path systemRom;
    std::array<std::filesystem::path, 4> floppies;
    std::array<std::filesystem::path, 2> cartridges, hardDisks;
    std::array<bool, 4> floppyReadOnly{};
    std::array<bool, 2> hardDiskReadOnly{};
    int memorySource = 0;
    bool memoryBytes = false;
    bool memoryHex = false;
    bool toolbar = true;
    bool statusBar = true;
    std::uint16_t traceMask = 03700;
    int keyboardLegend = 2; // Latin, Cyrillic, both, or follow the UKNC mode.
    int keyboardScale = 100;
    int uiTheme = 0;
    int uiScale = 100;
    std::string uiLayout;
};

inline constexpr std::size_t MaxUiLayoutBytes = 64 * 1024;

enum class ImportEncoding { unicode, windows1251, windows1252 };
// Imports recognized values into the supplied settings without changing the source.
// Unrecognized or invalid values leave the corresponding current setting intact.
[[nodiscard]] std::expected<Settings, IniFileError> ImportLegacy(
    const std::filesystem::path& path, ImportEncoding encoding, const Settings& current, bool importMedia = false);

[[nodiscard]] bool ValidWindowSize(int width, int height);
// Compares existing aliases and normalized paths that may not exist yet.
[[nodiscard]] bool SameFile(const std::filesystem::path& first, const std::filesystem::path& second);
// Missing files and invalid individual values use defaults. Other read errors
// are returned to the caller, which should avoid overwriting the file.
[[nodiscard]] std::expected<Settings, IniFileError> Load(const std::filesystem::path& path);
// Saves all values in one replacement while preserving other INI entries.
[[nodiscard]] std::expected<void, IniFileError> Save(const std::filesystem::path& path, const Settings& settings);
}
