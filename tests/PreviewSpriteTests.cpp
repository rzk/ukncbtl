/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "PreviewSprite.h"
#include "SpriteDecoder.h"
#include "emubase/Emubase.h"
#include <gtest/gtest.h>

void DebugLog(const wchar_t*) {}

TEST(PreviewSprite, CapturesCpuMemoryTopDownWithOpaqueColors)
{
    CProcessor::Init();
    {
        CMotherboard board;
        board.Reset();
        auto* cpu = board.GetCPU();
        cpu->SetPSW(0);
        auto* memory = cpu->GetMemoryController();
        memory->SetWord(01000, false, 0000001);
        const auto image = preview_sprite::Capture(board, 01000, 1, 0);
        ASSERT_TRUE(image);
        ASSERT_EQ(image->size(), sprite::Width * sprite::Height);
        EXPECT_EQ((*image)[0], 0xffffffffu);
        EXPECT_EQ((*image)[1], 0xff000000u);
        EXPECT_EQ((*image)[8], 0xff7f7f7fu);
        EXPECT_EQ((*image)[(sprite::Height - 1) * sprite::Width], 0xff000000u);
        memory->SetWord(01000, false, 0x0201);
        const auto color = preview_sprite::Capture(board, 01000, 1, 1);
        ASSERT_TRUE(color);
        EXPECT_EQ((*color)[0], 0xff00ff00u);
        EXPECT_EQ((*color)[1], 0xffff0000u);
        const auto odd = preview_sprite::Capture(board, 01001, 1, 0);
        ASSERT_TRUE(odd);
        EXPECT_EQ((*odd)[0], 0xff000000u);
        EXPECT_EQ((*odd)[1], 0xffffffffu);
        EXPECT_FALSE(preview_sprite::Capture(board, 0, 0, 0));
        EXPECT_FALSE(preview_sprite::Capture(board, 0, 1, 5));
    }
    CProcessor::Done();
}
