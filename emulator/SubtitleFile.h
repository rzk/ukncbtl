/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once

#include "Subtitles.h"
#include <expected>
#include <filesystem>
#include <vector>

namespace subtitles
{
enum class FileError { open_failed, read_failed, too_large, invalid_encoding, parse_failed };

// Reads UTF-16LE with or without a BOM. Input is limited to 1 MiB; text after
// the first UTF-16 NUL is ignored, as in the original subtitle loader.
[[nodiscard]] std::expected<std::vector<DisasmSubtitleItem>, FileError> LoadFile(const std::filesystem::path& path);
}
