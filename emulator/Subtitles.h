/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

enum class DisasmSubtitleType : std::uint8_t
{
    none = 0, comment = 1, blockComment = 2, data = 4,
};

constexpr DisasmSubtitleType operator|(DisasmSubtitleType left, DisasmSubtitleType right)
{
    return static_cast<DisasmSubtitleType>(std::to_underlying(left) | std::to_underlying(right));
}

[[nodiscard]] constexpr bool MatchesSubtitleType(DisasmSubtitleType type, DisasmSubtitleType mask)
{
    return (std::to_underlying(type) & std::to_underlying(mask)) != 0;
}

struct DisasmSubtitleItem
{
    std::uint16_t address{};
    DisasmSubtitleType type = DisasmSubtitleType::none;
    std::wstring comment;
};
namespace subtitles
{
enum class ParseError { Empty, BigEndian, InvalidAddress };
// Owns comment strings; input may be released or changed after parsing.
[[nodiscard]] std::expected<std::vector<DisasmSubtitleItem>, ParseError> Parse(std::wstring_view text);
[[nodiscard]] const DisasmSubtitleItem* Find(std::span<const DisasmSubtitleItem> items, std::uint16_t address,
    DisasmSubtitleType mask);
}
