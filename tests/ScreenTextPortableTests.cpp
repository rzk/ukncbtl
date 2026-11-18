/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "ScreenText.h"
#include "ScreenTextCapture.h"
#include "emubase/Emubase.h"
#include <gtest/gtest.h>
#include <array>
#include <string>

void DebugLog(const wchar_t*) {}

TEST(ScreenTextPortable, ConvertsUkncCharactersToUtf8)
{
    const std::array<std::uint8_t, 8> input{'A', 0xc1, 0xe1, 0x80, 0x9b, '\r', '\n', 0};
    const auto text = screen_text::ToUtf8(input);
    EXPECT_EQ(text, "A\xd0\xb0\xd0\x90\xe2\x86\x91\xe2\x96\x91\r\n");
    EXPECT_TRUE(screen_text::ToUtf8({}).empty());
}

TEST(ScreenTextPortable, CapturesTwentySixRowsFromBoard)
{
    CProcessor::Init();
    {
        CMotherboard board;
        board.Reset();
        const auto text = screen_text::Capture(board);
        ASSERT_TRUE(text);
        EXPECT_EQ(text->size(), 26 * 82);
        for (std::size_t row = 0; row < 26; ++row)
        {
            EXPECT_EQ(text->substr(row * 82, 80), std::string(80, ' '));
            EXPECT_EQ(text->substr(row * 82 + 80, 2), "\r\n");
        }
    }
    CProcessor::Done();
}
