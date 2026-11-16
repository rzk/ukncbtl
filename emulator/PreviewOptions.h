/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once
#include <array>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace preview_options
{
struct Options
{
    bool help = false, smokeTest = false;
    int boot = 0;
    std::optional<bool> run, sound, fullscreen;
    std::optional<std::filesystem::path> rom;
    std::array<std::optional<std::filesystem::path>, 4> floppies;
    std::array<std::optional<std::filesystem::path>, 2> cartridges, hardDisks;
};
// Arguments are UTF-8 and exclude the executable name.
[[nodiscard]] std::expected<Options, std::string> Parse(std::span<const std::string_view> arguments);
}
