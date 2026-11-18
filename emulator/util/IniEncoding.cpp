/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "IniEncoding.h"
#include <array>
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

std::expected<std::wstring, Error> DecodeLegacy(std::span<const std::uint8_t> bytes, LegacyEncoding encoding)
{
    static constexpr std::array<std::uint16_t, 64> cp1251{
        0x0402,0x0403,0x201a,0x0453,0x201e,0x2026,0x2020,0x2021,0x20ac,0x2030,0x0409,0x2039,0x040a,0x040c,0x040b,0x040f,
        0x0452,0x2018,0x2019,0x201c,0x201d,0x2022,0x2013,0x2014,0x0098,0x2122,0x0459,0x203a,0x045a,0x045c,0x045b,0x045f,
        0x00a0,0x040e,0x045e,0x0408,0x00a4,0x0490,0x00a6,0x00a7,0x0401,0x00a9,0x0404,0x00ab,0x00ac,0x00ad,0x00ae,0x0407,
        0x00b0,0x00b1,0x0406,0x0456,0x0491,0x00b5,0x00b6,0x00b7,0x0451,0x2116,0x0454,0x00bb,0x0458,0x0405,0x0455,0x0457};
    static constexpr std::array<std::uint16_t, 32> cp1252{
        0x20ac,0x0081,0x201a,0x0192,0x201e,0x2026,0x2020,0x2021,0x02c6,0x2030,0x0160,0x2039,0x0152,0x008d,0x017d,0x008f,
        0x0090,0x2018,0x2019,0x201c,0x201d,0x2022,0x2013,0x2014,0x02dc,0x2122,0x0161,0x203a,0x0153,0x009d,0x017e,0x0178};
    if (encoding != LegacyEncoding::windows_1251 && encoding != LegacyEncoding::windows_1252)
        return std::unexpected(Error::unsupported_encoding);
    if (Starts(bytes, {0xef, 0xbb, 0xbf}) || Starts(bytes, {0xff, 0xfe}) ||
        Starts(bytes, {0xfe, 0xff}) || Starts(bytes, {0x00, 0x00, 0xfe, 0xff}))
        return std::unexpected(Error::invalid_text);
    std::wstring text;
    text.reserve(bytes.size());
    for (const auto byte : bytes)
    {
        if (byte == 0) return std::unexpected(Error::invalid_text);
        std::uint32_t value = byte;
        if (encoding == LegacyEncoding::windows_1251 && byte >= 0x80)
            value = byte < 0xc0 ? cp1251[byte - 0x80] : 0x0410 + byte - 0xc0;
        else if (encoding == LegacyEncoding::windows_1252 && byte >= 0x80 && byte < 0xa0)
            value = cp1252[byte - 0x80];
        AppendWide(text, value);
    }
    return text;
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
