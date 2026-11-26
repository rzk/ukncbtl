/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "util/SettingsCodec.h"
#include <gtest/gtest.h>
#include <cstdint>
#include <string_view>

TEST(SettingsCodec, DecimalBoundariesAndLeadingZeros)
{
    EXPECT_EQ(settings_codec::DecodeUnsignedDecimal(L"0"), 0u);
    EXPECT_EQ(settings_codec::DecodeUnsignedDecimal(L"4294967295"), UINT32_MAX);
    EXPECT_EQ(settings_codec::DecodeUnsignedDecimal(L"0000123"), 123u);
}

TEST(SettingsCodec, RejectsMalformedDecimalAndOverflow)
{
    for (const auto text : {L"", L"-1", L"+1", L"1x", L" 1",
             L"1 ", L"0x10", L"1.0", L"4294967296", L"99999999999999999999"})
        EXPECT_FALSE(settings_codec::DecodeUnsignedDecimal(text)) << testing::PrintToString(text);
    EXPECT_FALSE(settings_codec::DecodeUnsignedDecimal(std::wstring_view(L"1\0x", 3)));
}
