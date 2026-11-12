/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "SubtitleFile.h"
#include "util/IniEncoding.h"
#include <array>
#include <cstdint>
#include <fstream>
#include <utility>
#include <vector>

std::expected<std::vector<DisasmSubtitleItem>, subtitles::FileError> subtitles::LoadFile(const std::filesystem::path& path)
{
    constexpr std::size_t maxBytes = 1024 * 1024;
    std::ifstream file(path, std::ios::binary);
    if (!file) return std::unexpected(FileError::open_failed);
    std::vector<std::uint8_t> bytes;
    std::array<char, 4096> chunk;
    while (file.read(chunk.data(), chunk.size()) || file.gcount() > 0)
    {
        const auto count = static_cast<std::size_t>(file.gcount());
        if (count > maxBytes - bytes.size()) return std::unexpected(FileError::too_large);
        bytes.insert(bytes.end(), chunk.begin(), chunk.begin() + count);
    }
    if (file.bad() || !file.eof()) return std::unexpected(FileError::read_failed);
    if (bytes.size() % 2 != 0) return std::unexpected(FileError::invalid_encoding);
    const bool bom = bytes.size() >= 2 && bytes[0] == 0xff && bytes[1] == 0xfe;
    if (bytes.size() >= 2 && bytes[0] == 0xfe && bytes[1] == 0xff)
        return std::unexpected(FileError::invalid_encoding);

    // Add a BOM for the shared UTF-16 decoder, and stop at the first NUL to
    // preserve the original parser's behavior even if later bytes are invalid.
    std::vector<std::uint8_t> normalized{0xff, 0xfe};
    for (std::size_t i = bom ? 2 : 0; i < bytes.size(); i += 2)
    {
        if (bytes[i] == 0 && bytes[i + 1] == 0) break;
        normalized.push_back(bytes[i]);
        normalized.push_back(bytes[i + 1]);
    }
    const auto decoded = ini_encoding::Decode(normalized);
    if (!decoded) return std::unexpected(FileError::invalid_encoding);
    // The parser treats a BOM-only document as valid and empty.
    auto parsed = Parse(bom && decoded->text.empty() ? std::wstring_view(L"\ufeff") : std::wstring_view(decoded->text));
    if (!parsed) return std::unexpected(FileError::parse_failed);
    return std::move(*parsed);
}
