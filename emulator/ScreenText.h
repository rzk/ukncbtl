/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once
#include <cstdint>
#include <cstddef>
#include <optional>
#include <span>
#include <string>

namespace screen_text
{
inline constexpr std::size_t FontSize = 224 * 11;
inline constexpr std::size_t FrameSize = 640 * 288;
inline constexpr std::size_t OutputSize = 82 * 26 + 1;
// Glyph spans use the screen's 640-pixel stride; invalid spans return no value.
std::optional<uint8_t> Recognize(std::span<const uint8_t> current, std::span<const uint8_t> standard,
    std::span<const uint32_t> pixels);
// On failure output is unchanged. Success writes 26 CRLF rows and a trailing zero.
bool Convert(std::span<const uint8_t> current, std::span<const uint8_t> standard,
    std::span<const uint32_t> pixels, std::span<uint8_t> output);
// Convert UKNC KOI8-R text to UTF-8, stopping at the first NUL.
[[nodiscard]] std::string ToUtf8(std::span<const uint8_t> text);
}
