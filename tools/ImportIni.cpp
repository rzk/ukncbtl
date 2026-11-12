/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "util/IniFile.h"
#include <filesystem>
#include <iostream>
#include <optional>
#include <string_view>

namespace
{
std::optional<ini_encoding::LegacyEncoding> ParseEncoding(std::string_view value)
{
    if (value == "windows-1251") return ini_encoding::LegacyEncoding::windows_1251;
    if (value == "windows-1252") return ini_encoding::LegacyEncoding::windows_1252;
    return std::nullopt;
}

std::string_view Describe(IniFileError error)
{
    switch (error)
    {
    case IniFileError::open_failed: return "could not open the source or create a temporary file";
    case IniFileError::read_failed: return "could not read the source file";
    case IniFileError::write_failed: return "could not write the converted file";
    case IniFileError::replace_failed: return "could not create the destination; hard-link support is required";
    case IniFileError::destination_exists: return "destination already exists";
    case IniFileError::too_large: return "file exceeds the 16 MiB limit";
    case IniFileError::invalid_text: return "source is not a supported legacy text file";
    case IniFileError::unsupported_encoding: return "unsupported source encoding";
    case IniFileError::invalid_entry: return "invalid settings entry";
    }
    return "unknown error";
}
}

int main(int argc, char* argv[])
{
    if (argc != 4 || !ParseEncoding(argv[1]))
    {
        std::cerr << "Usage: ukncbtl-import-ini <windows-1251|windows-1252> <source> <new-destination>\n";
        return 2;
    }
    try
    {
        const auto result = IniFile::ImportLegacy(std::filesystem::path(argv[2]),
            std::filesystem::path(argv[3]), *ParseEncoding(argv[1]));
        if (!result)
        {
            std::cerr << "Import failed: " << Describe(result.error()) << '\n';
            return 1;
        }
    }
    catch (const std::filesystem::filesystem_error&)
    {
        std::cerr << "Import failed: invalid filesystem path\n";
        return 1;
    }
    std::cout << "Imported settings as UTF-8.\n";
    return 0;
}
