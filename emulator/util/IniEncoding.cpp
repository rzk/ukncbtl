/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "IniEncoding.h"
#include <initializer_list>

namespace ini_encoding
{
namespace
{
bool Scalar(std::uint32_t value)
{
    return value <= 0x10ffff && !(value >= 0xd800 && value <= 0xdfff);
}

void AppendWide(std::wstring& text, std::uint32_t value)
{
    if constexpr (sizeof(wchar_t) == 2)
    {
        if (value > 0xffff)
        {
            value -= 0x10000;
            text.push_back(static_cast<wchar_t>(0xd800 + (value >> 10)));
            text.push_back(static_cast<wchar_t>(0xdc00 + (value & 0x3ff)));
            return;
        }
    }
    text.push_back(static_cast<wchar_t>(value));
}

std::expected<std::uint32_t, Error> NextWide(std::wstring_view text, std::size_t& position)
{
    auto value = static_cast<std::uint32_t>(text[position++]);
    if constexpr (sizeof(wchar_t) == 2)
    {
        if (value >= 0xd800 && value <= 0xdbff)
        {
            if (position == text.size()) return std::unexpected(Error::invalid_text);
            const auto low = static_cast<std::uint32_t>(text[position++]);
            if (low < 0xdc00 || low > 0xdfff) return std::unexpected(Error::invalid_text);
            value = 0x10000 + ((value - 0xd800) << 10) + (low - 0xdc00);
        }
    }
    if (!Scalar(value)) return std::unexpected(Error::invalid_text);
    return value;
}

bool Starts(std::span<const std::uint8_t> bytes, std::initializer_list<std::uint8_t> prefix)
{
    if (bytes.size() < prefix.size()) return false;
    std::size_t i = 0;
    for (const auto byte : prefix) if (bytes[i++] != byte) return false;
    return true;
}
}

std::expected<DecodedText, Error> Decode(std::span<const std::uint8_t> bytes)
{
    if (Starts(bytes, {0xff, 0xfe, 0x00, 0x00}) || Starts(bytes, {0x00, 0x00, 0xfe, 0xff}) ||
        Starts(bytes, {0xfe, 0xff})) return std::unexpected(Error::unsupported_encoding);
    DecodedText result{{}, Encoding::utf8};
    if (Starts(bytes, {0xff, 0xfe})) { result.encoding = Encoding::utf16_le; bytes = bytes.subspan(2); }
    else if (Starts(bytes, {0xef, 0xbb, 0xbf})) { result.encoding = Encoding::utf8_bom; bytes = bytes.subspan(3); }

    std::size_t i = 0;
    while (i < bytes.size())
    {
        std::uint32_t value;
        if (result.encoding == Encoding::utf16_le)
        {
            if (bytes.size() - i < 2) return std::unexpected(Error::invalid_text);
            value = bytes[i] | (static_cast<std::uint32_t>(bytes[i + 1]) << 8);
            i += 2;
            if (value >= 0xd800 && value <= 0xdbff)
            {
                if (bytes.size() - i < 2) return std::unexpected(Error::invalid_text);
                const auto low = bytes[i] | (static_cast<std::uint32_t>(bytes[i + 1]) << 8);
                if (low < 0xdc00 || low > 0xdfff) return std::unexpected(Error::invalid_text);
                i += 2;
                value = 0x10000 + ((value - 0xd800) << 10) + (low - 0xdc00);
            }
        }
        else
        {
            const auto first = bytes[i++];
            std::size_t extra = 0;
            std::uint32_t minimum = 0;
            if (first < 0x80) value = first;
            else if (first >= 0xc2 && first <= 0xdf) { value = first & 0x1f; extra = 1; minimum = 0x80; }
            else if (first >= 0xe0 && first <= 0xef) { value = first & 0x0f; extra = 2; minimum = 0x800; }
            else if (first >= 0xf0 && first <= 0xf4) { value = first & 0x07; extra = 3; minimum = 0x10000; }
            else return std::unexpected(Error::invalid_text);
            if (bytes.size() - i < extra) return std::unexpected(Error::invalid_text);
            for (std::size_t j = 0; j < extra; ++j)
            {
                const auto next = bytes[i++];
                if ((next & 0xc0) != 0x80) return std::unexpected(Error::invalid_text);
                value = (value << 6) | (next & 0x3f);
            }
            if (value < minimum) return std::unexpected(Error::invalid_text);
        }
        if (!Scalar(value)) return std::unexpected(Error::invalid_text);
        AppendWide(result.text, value);
    }
    return result;
}

std::expected<std::vector<std::uint8_t>, Error> Encode(std::wstring_view text, Encoding encoding)
{
    std::vector<std::uint8_t> bytes;
    if (encoding == Encoding::utf8_bom) bytes = {0xef, 0xbb, 0xbf};
    else if (encoding == Encoding::utf16_le) bytes = {0xff, 0xfe};
    else if (encoding != Encoding::utf8) return std::unexpected(Error::unsupported_encoding);
    for (std::size_t i = 0; i < text.size();)
    {
        const auto next = NextWide(text, i);
        if (!next) return std::unexpected(next.error());
        auto value = *next;
        if (encoding == Encoding::utf16_le)
        {
            auto put = [&](std::uint32_t unit) { bytes.push_back(static_cast<std::uint8_t>(unit)); bytes.push_back(static_cast<std::uint8_t>(unit >> 8)); };
            if (value <= 0xffff) put(value);
            else { value -= 0x10000; put(0xd800 + (value >> 10)); put(0xdc00 + (value & 0x3ff)); }
        }
        else if (value < 0x80) bytes.push_back(static_cast<std::uint8_t>(value));
        else
        {
            int shift = value < 0x800 ? 6 : value < 0x10000 ? 12 : 18;
            bytes.push_back(static_cast<std::uint8_t>((shift == 6 ? 0xc0 : shift == 12 ? 0xe0 : 0xf0) | (value >> shift)));
            for (shift -= 6; shift >= 0; shift -= 6) bytes.push_back(static_cast<std::uint8_t>(0x80 | ((value >> shift) & 0x3f)));
        }
    }
    return bytes;
}
}
