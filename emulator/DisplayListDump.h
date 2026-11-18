/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once

#include "VideoDecoder.h"
#include <expected>
#include <filesystem>
#include <iosfwd>
#include <string>

namespace display_list_dump
{
enum class FileError { open_failed, write_failed, close_failed };

[[nodiscard]] std::string FormatLine(int line, const video::DisplayTag& tag);
[[nodiscard]] bool Write(std::ostream& output, const CMotherboard& board);
[[nodiscard]] bool Write(std::ostream& output, const video::MemorySnapshot& snapshot);
// Replaces the destination; write failures may leave a partial file.
[[nodiscard]] std::expected<void, FileError> Save(const std::filesystem::path& path, const CMotherboard& board);
[[nodiscard]] std::expected<void, FileError> Save(const std::filesystem::path& path, const video::MemorySnapshot& snapshot);
}
