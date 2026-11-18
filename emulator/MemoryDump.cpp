/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "MemoryDump.h"
#include <algorithm>
#include <array>
#include <fstream>
#include <ostream>

std::optional<memory_dump::WordRange> memory_dump::Range(std::uint16_t start, std::uint16_t end)
{
    if (end <= start) return std::nullopt;
    return WordRange{static_cast<std::uint16_t>(start / 2), static_cast<std::uint16_t>((end - 1) / 2)};
}

bool memory_dump::WriteWords(std::ostream& output, std::span<const std::uint16_t> words)
{
    std::array<char, 4096> bytes{};
    while (!words.empty())
    {
        const auto count = (std::min)(words.size(), bytes.size() / 2);
        for (std::size_t i = 0; i < count; ++i)
        {
            bytes[2 * i] = static_cast<char>(words[i] & 0xff);
            bytes[2 * i + 1] = static_cast<char>(words[i] >> 8);
        }
        output.write(bytes.data(), static_cast<std::streamsize>(count * 2));
        if (!output) return false;
        words = words.subspan(count);
    }
    return static_cast<bool>(output);
}

std::expected<void, memory_dump::FileError> memory_dump::Save(
    const std::filesystem::path& path, std::span<const std::uint16_t> words)
{
    if (path.empty()) return std::unexpected(FileError::invalid_path);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) return std::unexpected(FileError::open_failed);
    const bool written = WriteWords(file, words);
    file.flush();
    const bool flushed = static_cast<bool>(file);
    file.close();
    if (!written || !flushed) return std::unexpected(FileError::write_failed);
    if (!file) return std::unexpected(FileError::close_failed);
    return {};
}
