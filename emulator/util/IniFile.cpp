/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "IniFile.h"
#include <array>
#include <atomic>
#include <chrono>
#include <fstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace
{
// Creating the directory is an exclusive operation. Its unique name avoids
// opening or deleting an existing file even when multiple writers race.
class TemporaryDirectory
{
public:
    explicit TemporaryDirectory(std::filesystem::path path) : path_(std::move(path)) {}
    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;
    ~TemporaryDirectory()
    {
        std::error_code error;
        std::filesystem::remove(path_ / "settings", error);
        std::filesystem::remove(path_, error);
    }
    const std::filesystem::path& Path() const { return path_; }
private:
    std::filesystem::path path_;
};

std::expected<std::filesystem::path, IniFileError> CreateTemporaryDirectory(const std::filesystem::path& destination)
{
    static std::atomic<std::uint64_t> sequence{0};
    const auto parent = destination.has_parent_path() ? destination.parent_path() : std::filesystem::path(L".");
    for (int attempt = 0; attempt < 128; ++attempt)
    {
        const auto time = std::chrono::steady_clock::now().time_since_epoch().count();
        const auto name = std::string(".ukncbtl-save-") + std::to_string(time) + "-" + std::to_string(sequence.fetch_add(1));
        const auto path = parent / name;
        std::error_code error;
        if (std::filesystem::create_directory(path, error)) return path;
        if (error && error != std::errc::file_exists) return std::unexpected(IniFileError::open_failed);
    }
    return std::unexpected(IniFileError::open_failed);
}

IniFileError EncodingError(ini_encoding::Error error)
{
    return error == ini_encoding::Error::unsupported_encoding
        ? IniFileError::unsupported_encoding : IniFileError::invalid_text;
}

std::expected<std::vector<std::uint8_t>, IniFileError> ReadBytes(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) return std::unexpected(IniFileError::open_failed);
    std::vector<std::uint8_t> bytes;
    std::array<char, 4096> chunk;
    while (file.read(chunk.data(), chunk.size()) || file.gcount() > 0)
    {
        const auto count = static_cast<std::size_t>(file.gcount());
        if (count > IniFile::MaxBytes - bytes.size()) return std::unexpected(IniFileError::too_large);
        bytes.insert(bytes.end(), chunk.begin(), chunk.begin() + count);
    }
    if (file.bad() || !file.eof()) return std::unexpected(IniFileError::read_failed);
    return bytes;
}
}

std::expected<IniFile, IniFileError> IniFile::Open(const std::filesystem::path& path)
{
    const auto bytes = ReadBytes(path);
    if (!bytes) return std::unexpected(bytes.error());
    const auto decoded = ini_encoding::Decode(*bytes);
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
    if (encoded->size() > MaxBytes) return std::unexpected(IniFileError::too_large);
    const auto temporary = CreateTemporaryDirectory(path);
    if (!temporary) return std::unexpected(temporary.error());
    const TemporaryDirectory cleanup(*temporary);
    const auto staged = cleanup.Path() / "settings";
    std::ofstream file(staged, std::ios::binary | std::ios::trunc);
    if (!file) return std::unexpected(IniFileError::open_failed);
    if (!encoded->empty())
        file.write(reinterpret_cast<const char*>(encoded->data()), static_cast<std::streamsize>(encoded->size()));
    file.close();
    if (!file) return std::unexpected(IniFileError::write_failed);
    std::error_code error;
    std::filesystem::rename(staged, path, error);
    if (error) return std::unexpected(IniFileError::replace_failed);
    return {};
}
