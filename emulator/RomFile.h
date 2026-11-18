/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>

namespace rom_file
{
enum class Error { invalid_argument, open_failed, read_failed };

// Reads exactly output.size() bytes from the start, ignoring any trailing data.
// On failure the output is unchanged. Callers must coordinate external writes.
[[nodiscard]] std::expected<void, Error> ReadPrefix(
    const std::filesystem::path& path, std::span<std::uint8_t> output);
}
