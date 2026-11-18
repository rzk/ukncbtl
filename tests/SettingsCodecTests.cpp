/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "util/SettingsCodec.h"
#include <gtest/gtest.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

constexpr uint32_t Color(uint8_t r, uint8_t g, uint8_t b)
{ return static_cast<uint32_t>(r) | (static_cast<uint32_t>(g) << 8) | (static_cast<uint32_t>(b) << 16); }

TEST(SettingsCodec, DecimalBoundariesAndLeadingZeros)
{
    EXPECT_EQ(settings_codec::DecodeDword(L"0"), 0u);
    EXPECT_EQ(settings_codec::DecodeDword(L"4294967295"), UINT32_MAX);
    EXPECT_EQ(settings_codec::DecodeDword(L"0000123"), 123u);
}

TEST(SettingsCodec, RejectsMalformedDecimalAndOverflow)
{
    for (const auto text : {L"", L"-1", L"+1", L"1x", L" 1",
             L"1 ", L"0x10", L"1.0", L"4294967296", L"99999999999999999999"})
        EXPECT_FALSE(settings_codec::DecodeDword(text)) << testing::PrintToString(text);
    EXPECT_FALSE(settings_codec::DecodeDword(std::wstring_view(L"1\0x", 3)));
}

TEST(SettingsCodec, ColorEncodingUsesRgbOrderAndSixUppercaseDigits)
{
    EXPECT_EQ(settings_codec::EncodeColor(Color(0x12, 0x34, 0xab)), L"1234AB");
    EXPECT_EQ(settings_codec::EncodeColor(0), L"000000");
    EXPECT_EQ(settings_codec::EncodeColor(0xffffffff), L"FFFFFF");
    EXPECT_EQ(settings_codec::DecodeColor(L"1234ab"), Color(0x12, 0x34, 0xab));
    EXPECT_EQ(settings_codec::DecodeColor(L"F"), Color(0, 0, 15));
}

TEST(SettingsCodec, RejectsInvalidOrOutOfRangeColors)
{
    for (const auto text : {L"", L"#FFFFFF", L"-1", L"FFFFFG", L"FFFFFFx",
                            L"1000000", L"FFFFFFFF", L"FF FF"})
        EXPECT_FALSE(settings_codec::DecodeColor(text));
}

TEST(SettingsCodec, BinaryRoundTripsEveryByteAndAcceptsLowercase)
{
    std::array<uint8_t, 256> source{}, target{};
    for (std::size_t i = 0; i < source.size(); ++i) source[i] = static_cast<uint8_t>(i);
    auto text = settings_codec::EncodeBinary(source);
    EXPECT_EQ(text.size(), 512u);
    EXPECT_EQ(text.substr(0, 8), L"00010203");
    EXPECT_EQ(text.substr(508), L"FEFF");
    for (auto& ch : text) if (ch >= L'A' && ch <= L'F') ch += L'a' - L'A';
    EXPECT_TRUE(settings_codec::DecodeBinary(text, target));
    EXPECT_EQ(target, source);
    EXPECT_EQ(settings_codec::EncodeBinary({}), L"");
    EXPECT_TRUE(settings_codec::DecodeBinary(L"", {}));
}

TEST(SettingsCodec, BinaryFailureDoesNotPartiallyOverwriteOutput)
{
    for (const auto text : {L"", L"01", L"010", L"010203", L"01G0", L"010 "})
    {
        std::array<uint8_t, 4> guarded{0xaa, 0xbb, 0xcc, 0xdd};
        const auto before = guarded;
        EXPECT_FALSE(settings_codec::DecodeBinary(text, std::span{guarded}.subspan<1, 2>()));
        EXPECT_EQ(guarded, before);
    }
}

TEST(SettingsCodec, BinarySuccessPreservesSurroundingBytes)
{
    std::array<uint8_t, 4> bytes{0xaa, 0, 0, 0xdd};
    EXPECT_TRUE(settings_codec::DecodeBinary(L"01fE", std::span{bytes}.subspan<1, 2>()));
    EXPECT_EQ(bytes, (std::array<uint8_t, 4>{0xaa, 1, 0xfe, 0xdd}));
}
