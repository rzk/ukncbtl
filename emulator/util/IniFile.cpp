/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "IniFile.h"
#include <array>
#include <fstream>
#include <vector>

namespace
{
constexpr std::size_t MaxIniBytes = 16 * 1024 * 1024;

IniFileError EncodingError(ini_encoding::Error error)
{
    return error == ini_encoding::Error::unsupported_encoding
        ? IniFileError::unsupported_encoding : IniFileError::invalid_text;
}
}

std::expected<IniFile, IniFileError> IniFile::Open(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) return std::unexpected(IniFileError::open_failed);
    std::vector<std::uint8_t> bytes;
    std::array<char, 4096> chunk;
    while (file.read(chunk.data(), chunk.size()) || file.gcount() > 0)
    {
        const auto count = static_cast<std::size_t>(file.gcount());
        if (count > MaxIniBytes - bytes.size()) return std::unexpected(IniFileError::too_large);
        bytes.insert(bytes.end(), chunk.begin(), chunk.begin() + count);
    }
    if (file.bad() || !file.eof()) return std::unexpected(IniFileError::read_failed);
    const auto decoded = ini_encoding::Decode(bytes);
    if (!decoded) return std::unexpected(EncodingError(decoded.error()));
    IniFile result;
    result.document_ = IniDocument(decoded->text);
    result.encoding_ = decoded->encoding;
    return result;
}

std::expected<void, IniFileError> IniFile::Save(const std::filesystem::path& path) const
{
    const auto encoded = ini_encoding::Encode(document_.Serialize(), encoding_);
    if (!encoded) return std::unexpected(EncodingError(encoded.error()));
    if (encoded->size() > MaxIniBytes) return std::unexpected(IniFileError::too_large);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) return std::unexpected(IniFileError::open_failed);
    if (!encoded->empty())
        file.write(reinterpret_cast<const char*>(encoded->data()), static_cast<std::streamsize>(encoded->size()));
    file.close();
    if (!file) return std::unexpected(IniFileError::write_failed);
    return {};
}
