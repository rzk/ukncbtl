/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>

namespace disk_image
{
enum class FileError { invalid_argument, open_failed, write_failed, close_failed };

// A writer passed to CreatePortable must be closed before the call. Open
// truncates the destination; Write reports how many bytes were written.
class FileWriter
{
public:
    virtual ~FileWriter() = default;
    virtual bool Open(const std::filesystem::path& path) = 0;
    virtual std::size_t Write(std::span<const std::uint8_t> bytes) = 0;
    virtual bool Close() = 0;
};

// Supports 40 or 80 tracks. Existing files are truncated; an I/O failure may
// leave a partial image. Invalid arguments do not touch the destination.
[[nodiscard]] std::expected<void, FileError> CreatePortable(const std::filesystem::path& path, int tracks);
[[nodiscard]] std::expected<void, FileError> CreatePortable(const std::filesystem::path& path, int tracks, FileWriter& writer);
}
