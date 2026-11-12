/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ini_encoding
{
enum class Encoding { utf8, utf8_bom, utf16_le };
enum class Error { invalid_text, unsupported_encoding };
enum class LegacyEncoding { windows_1251, windows_1252 };

struct DecodedText
{
    std::wstring text;
    Encoding encoding;
};

// BOM-less input is interpreted as UTF-8. Legacy Windows code-page files are
// not automatically converted. UTF-16LE requires a BOM; UTF-16BE and UTF-32
// BOMs are rejected rather than interpreted as another format. The UTF-32LE
// BOM is indistinguishable from UTF-16LE followed by NUL and takes precedence.
[[nodiscard]] std::expected<DecodedText, Error> Decode(std::span<const std::uint8_t> bytes);
// Explicit import only; no code page is guessed from the input. Unicode BOMs
// and embedded NULs are rejected to avoid silently importing the wrong format.
[[nodiscard]] std::expected<std::wstring, Error> DecodeLegacy(std::span<const std::uint8_t> bytes, LegacyEncoding encoding);
[[nodiscard]] std::expected<std::vector<std::uint8_t>, Error> Encode(std::wstring_view text, Encoding encoding);
}
