/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once

#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>

namespace settings_codec
{
inline std::optional<uint32_t> DecodeUnsignedDecimal(std::wstring_view text)
{
    if (text.empty()) return std::nullopt;
    constexpr auto maximum = std::numeric_limits<uint32_t>::max();
    uint32_t value = 0;
    for (const auto ch : text)
    {
        if (ch < L'0' || ch > L'9') return std::nullopt;
        const auto digit = static_cast<uint32_t>(ch - L'0');
        if (value > (maximum - digit) / 10) return std::nullopt;
        value = value * 10 + digit;
    }
    return value;
}
}
