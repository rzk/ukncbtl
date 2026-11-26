/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "ScreenText.h"
#include "ScreenTextCapture.h"
#include "emubase/Emubase.h"
#include <gtest/gtest.h>
#include <array>
#include <future>
#include <memory>
#include <string>
#include <utility>

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

TEST(ScreenTextPortable, WorkerRecognizesDisplayAndFontsFromTheSameSnapshot)
{
    CProcessor::Init();
    struct CoreLifetime { ~CoreLifetime() { CProcessor::Done(); } } lifetime;
    CMotherboard board;
    board.Reset();
    constexpr std::uint16_t displayTag = 0270, pixelAddress = 0x5000, glyphAddress = 0x6000;
    constexpr std::uint16_t fontTable = 014142 + 32 * 2;
    board.SetRAMWord(0, displayTag, pixelAddress);
    board.SetRAMWord(0, displayTag + 2, displayTag);
    board.SetRAMByte(2, pixelAddress, 0xa5);
    auto* ppu = board.GetPPUMemoryController();
    ppu->SetWord(fontTable + ('A' - ' ') * 2, false, glyphAddress);
    for (std::uint16_t row = 0; row < 11; ++row)
        ppu->SetByte(glyphAddress + row, false, 0xa5);

    auto input = std::make_unique<screen_text::CaptureInput>();
    screen_text::Capture(board, *input);
    auto recognition = std::async(std::launch::async, [input = std::move(input)] {
        return screen_text::Recognize(*input);
    });
    board.SetRAMByte(2, pixelAddress, 0);
    ppu->SetWord(fontTable + ('A' - ' ') * 2, false, 0);

    const auto text = recognition.get();
    ASSERT_TRUE(text);
    EXPECT_TRUE(text->starts_with("A" + std::string(79, ' ') + "\r\n"));
    const auto current = screen_text::Capture(board);
    ASSERT_TRUE(current);
    EXPECT_TRUE(current->starts_with(std::string(80, ' ') + "\r\n"));
}
