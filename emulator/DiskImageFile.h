/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once

#include <expected>
#include <filesystem>

namespace disk_image
{
enum class FileError { invalid_argument, open_failed, write_failed, close_failed };

// Supports 40 or 80 tracks. Existing files are truncated; an I/O failure may
// leave a partial image. Invalid arguments do not touch the destination.
[[nodiscard]] std::expected<void, FileError> CreatePortable(const std::filesystem::path& path, int tracks);
}
