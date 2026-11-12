/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once

#include "IniDocument.h"
#include "IniEncoding.h"
#include <expected>
#include <filesystem>

enum class IniFileError
{
    open_failed,
    read_failed,
    write_failed,
    too_large,
    invalid_text,
    unsupported_encoding,
};

// A snapshot of an INI file. Callers must coordinate concurrent access.
// Files larger than 16 MiB are rejected. Save writes in place and is not atomic;
// a write failure may leave a partial file.
class IniFile
{
public:
    // New documents use UTF-8 without a BOM.
    IniFile() = default;
    [[nodiscard]] static std::expected<IniFile, IniFileError> Open(const std::filesystem::path& path);
    [[nodiscard]] std::expected<void, IniFileError> Save(const std::filesystem::path& path) const;

    [[nodiscard]] IniDocument& Document() { return document_; }
    [[nodiscard]] const IniDocument& Document() const { return document_; }
    [[nodiscard]] ini_encoding::Encoding Encoding() const { return encoding_; }

private:
    IniDocument document_;
    ini_encoding::Encoding encoding_ = ini_encoding::Encoding::utf8;
};
