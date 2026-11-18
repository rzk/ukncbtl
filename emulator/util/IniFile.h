/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once

#include "IniDocument.h"
#include "IniEncoding.h"
#include <cstddef>
#include <expected>
#include <filesystem>

enum class IniFileError
{
    open_failed,
    read_failed,
    write_failed,
    replace_failed,
    destination_exists,
    too_large,
    invalid_text,
    unsupported_encoding,
    invalid_entry,
};

// A snapshot of an INI file. Callers must coordinate concurrent access.
// Files larger than 16 MiB are rejected. Save writes to a temporary file on
// the same filesystem before replacing the destination. It does not guarantee
// durability across power loss or preserve the destination's file metadata.
class IniFile
{
public:
    static constexpr std::size_t MaxBytes = 16 * 1024 * 1024;
    // New documents use UTF-8 without a BOM.
    IniFile() = default;
    [[nodiscard]] static std::expected<IniFile, IniFileError> Open(const std::filesystem::path& path);
    // Explicitly decode an old code-page file as a UTF-8 snapshot. OpenLegacy
    // never modifies the source; callers choose where to save the result.
    [[nodiscard]] static std::expected<IniFile, IniFileError> OpenLegacy(
        const std::filesystem::path& path, ini_encoding::LegacyEncoding encoding);
    // Import to a new UTF-8 file without replacing an existing path. Publishing
    // requires hard-link support on the destination filesystem.
    [[nodiscard]] static std::expected<void, IniFileError> ImportLegacy(
        const std::filesystem::path& source, const std::filesystem::path& destination,
        ini_encoding::LegacyEncoding encoding);
    [[nodiscard]] std::expected<void, IniFileError> Save(const std::filesystem::path& path) const;

    [[nodiscard]] IniDocument& Document() { return document_; }
    [[nodiscard]] const IniDocument& Document() const { return document_; }
    [[nodiscard]] ini_encoding::Encoding Encoding() const { return encoding_; }

private:
    [[nodiscard]] std::expected<void, IniFileError> SaveImpl(const std::filesystem::path& path, bool replace) const;
    IniDocument document_;
    ini_encoding::Encoding encoding_ = ini_encoding::Encoding::utf8;
};
