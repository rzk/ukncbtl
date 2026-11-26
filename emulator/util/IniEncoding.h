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

struct DecodedText
{
    std::wstring text;
    Encoding encoding;
};

// BOM-less input is interpreted as UTF-8. UTF-16LE requires a BOM. UTF-16BE and UTF-32
// BOMs are rejected rather than interpreted as another format. The UTF-32LE
// BOM is indistinguishable from UTF-16LE followed by NUL and takes precedence.
[[nodiscard]] std::expected<DecodedText, Error> Decode(std::span<const std::uint8_t> bytes);
[[nodiscard]] std::expected<std::vector<std::uint8_t>, Error> Encode(std::wstring_view text, Encoding encoding);
}
