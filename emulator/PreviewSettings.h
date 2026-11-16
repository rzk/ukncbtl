/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once

#include "util/IniFile.h"
#include <expected>
#include <filesystem>

namespace preview_settings
{
struct Settings
{
    int windowWidth = 960;
    int windowHeight = 720;
    int screenshotMode = 1;
    bool sound = true;
    int displayMode = 1;
    int paletteMode = 0;
};

[[nodiscard]] bool ValidWindowSize(int width, int height);
// Missing files and invalid individual values use defaults. Other read errors
// are returned to the caller, which should avoid overwriting the file.
[[nodiscard]] std::expected<Settings, IniFileError> Load(const std::filesystem::path& path);
// Saves all values in one replacement while preserving other INI entries.
[[nodiscard]] std::expected<void, IniFileError> Save(const std::filesystem::path& path, const Settings& settings);
}
