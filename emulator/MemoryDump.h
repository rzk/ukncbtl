/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <iosfwd>
#include <optional>
#include <span>

namespace memory_dump
{
struct WordRange
{
    std::uint16_t first;
    std::uint16_t last;
    [[nodiscard]] std::size_t Count() const { return static_cast<std::size_t>(last) - first + 1; }
};

// The end is exclusive; both endpoints are rounded to cover whole words.
[[nodiscard]] std::optional<WordRange> Range(std::uint16_t start, std::uint16_t end);
// Write each word as two little-endian bytes without closing the stream.
[[nodiscard]] bool WriteWords(std::ostream& output, std::span<const std::uint16_t> words);

enum class FileError { invalid_path, open_failed, write_failed, close_failed };
// Existing files are truncated; an I/O failure may leave a partial dump.
[[nodiscard]] std::expected<void, FileError> Save(const std::filesystem::path& path, std::span<const std::uint16_t> words);
}
