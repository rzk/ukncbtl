/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once
#include <cstdint>
#include <expected>
#include <limits>
#include <string_view>

enum class OctalError { empty, invalid_digit, out_of_range };

[[nodiscard]] inline std::expected<std::uint16_t, OctalError> ParseOctalValue(std::wstring_view text)
{
    if (text.empty()) return std::unexpected(OctalError::empty);
    std::uint32_t value = 0;
    for (const wchar_t ch : text)
    {
        if (ch < L'0' || ch > L'7') return std::unexpected(OctalError::invalid_digit);
        const auto digit = static_cast<std::uint32_t>(ch - L'0');
        if (value > ((std::numeric_limits<std::uint16_t>::max)() - digit) / 8)
            return std::unexpected(OctalError::out_of_range);
        value = value * 8 + digit;
    }
    return static_cast<std::uint16_t>(value);
}
