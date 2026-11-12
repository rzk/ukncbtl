/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "util/IniDocument.h"
#include "util/IniEncoding.h"
#include <gtest/gtest.h>
#include <array>
#include <vector>

using namespace ini_encoding;

TEST(IniEncoding, DecodesKnownUtf8AndUtf16AndPreservesEncoding)
{
    const std::vector<std::uint8_t> utf8{0x41, 0xc3, 0xa9, 0xe6, 0x97, 0xa5, 0xf0, 0x9f, 0x98, 0x80};
    const std::vector<std::uint8_t> utf16{0xff, 0xfe, 0x41, 0, 0xe9, 0, 0xe5, 0x65, 0x3d, 0xd8, 0, 0xde};
    for (const auto& [bytes, format] : std::array{
             std::pair{utf8, Encoding::utf8}, std::pair{utf16, Encoding::utf16_le}})
    {
        const auto decoded = Decode(bytes);
        ASSERT_TRUE(decoded);
        EXPECT_EQ(decoded->text, L"A\u00e9\u65e5\U0001f600");
        EXPECT_EQ(decoded->encoding, format);
        const auto encoded = Encode(decoded->text, format);
        ASSERT_TRUE(encoded);
        EXPECT_EQ(*encoded, bytes);
    }
}

TEST(IniEncoding, Utf8BomAndEmptyInputsRoundTrip)
{
    for (const auto format : {Encoding::utf8, Encoding::utf8_bom, Encoding::utf16_le})
    {
        const auto bytes = Encode(L"", format);
        ASSERT_TRUE(bytes);
        const auto decoded = Decode(*bytes);
        ASSERT_TRUE(decoded);
        EXPECT_EQ(decoded->text, L"");
        EXPECT_EQ(decoded->encoding, format);
    }
    const std::vector<std::uint8_t> bytes{0xef, 0xbb, 0xbf, 0x61};
    const auto decoded = Decode(bytes);
    ASSERT_TRUE(decoded);
    EXPECT_EQ(decoded->text, L"a");
    EXPECT_EQ(decoded->encoding, Encoding::utf8_bom);
    EXPECT_EQ(Encode(decoded->text, decoded->encoding).value(), bytes);
}

TEST(IniEncoding, RejectsMalformedUtf8)
{
    const std::vector<std::vector<std::uint8_t>> bad{
        {0x80}, {0xc0, 0x80}, {0xe0, 0x80, 0x80}, {0xed, 0xa0, 0x80},
        {0xf0, 0x80, 0x80, 0x80}, {0xf4, 0x90, 0x80, 0x80}, {0xf5, 0x80, 0x80, 0x80},
        {0xe2, 0x82}, {0xc2, 0x41}, {0xff}
    };
    for (const auto& bytes : bad)
    {
        const auto decoded = Decode(bytes);
        ASSERT_FALSE(decoded);
        EXPECT_EQ(decoded.error(), Error::invalid_text);
    }
}

TEST(IniEncoding, RejectsMalformedUtf16AndUnsupportedBoms)
{
    for (const auto& bytes : std::vector<std::vector<std::uint8_t>>{
             {0xff, 0xfe, 0x41}, {0xff, 0xfe, 0x00, 0xd8},
             {0xff, 0xfe, 0x00, 0xdc}, {0xff, 0xfe, 0x00, 0xd8, 0x41, 0x00}})
    {
        const auto decoded = Decode(bytes);
        ASSERT_FALSE(decoded);
        EXPECT_EQ(decoded.error(), Error::invalid_text);
    }
    for (const auto& bytes : std::vector<std::vector<std::uint8_t>>{
             {0xfe, 0xff}, {0xff, 0xfe, 0x00, 0x00}, {0x00, 0x00, 0xfe, 0xff}})
    {
        const auto decoded = Decode(bytes);
        ASSERT_FALSE(decoded);
        EXPECT_EQ(decoded.error(), Error::unsupported_encoding);
    }
}

TEST(IniEncoding, RejectsInvalidWideText)
{
    const std::wstring invalid(1, static_cast<wchar_t>(0xd800));
    for (const auto format : {Encoding::utf8, Encoding::utf8_bom, Encoding::utf16_le})
    {
        const auto encoded = Encode(invalid, format);
        ASSERT_FALSE(encoded);
        EXPECT_EQ(encoded.error(), Error::invalid_text);
    }
}

TEST(IniEncoding, DocumentUpdatePreservesUtf16AndUnicodePaths)
{
    const auto initial = Encode(L"[UKNCBTL]\r\nPath=\u65e5\u672c/\U0001f600\r\n", Encoding::utf16_le);
    ASSERT_TRUE(initial);
    const auto decoded = Decode(*initial);
    ASSERT_TRUE(decoded);
    IniDocument document(decoded->text);
    ASSERT_TRUE(document.Set(L"UKNCBTL", L"Sound", L"1"));
    const auto saved = Encode(document.Serialize(), decoded->encoding);
    ASSERT_TRUE(saved);
    const auto reopened = Decode(*saved);
    ASSERT_TRUE(reopened);
    EXPECT_EQ(reopened->encoding, Encoding::utf16_le);
    EXPECT_EQ(IniDocument(reopened->text).Get(L"UKNCBTL", L"Path"), L"\u65e5\u672c/\U0001f600");
    EXPECT_EQ(IniDocument(reopened->text).Get(L"UKNCBTL", L"Sound"), L"1");
}
