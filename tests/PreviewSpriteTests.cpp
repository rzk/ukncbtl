/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "PreviewSprite.h"
#include "SpriteDecoder.h"
#include "emubase/Emubase.h"
#include <gtest/gtest.h>
#include <chrono>
#include <thread>

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

namespace
{
class SpriteImageTest : public testing::Test
{
protected:
    void SetUp() override { CProcessor::Init(); }
    void TearDown() override { CProcessor::Done(); }
};
}

TEST_F(SpriteImageTest, ReusesUnchangedPixelsAndMatchesTheRequestedSelection)
{
    CMotherboard board;
    board.Reset();
    board.GetCPU()->SetPSW(0);
    const auto* cpu = board.GetCPU();
    auto* memory = board.GetCPU()->GetMemoryController();
    memory->SetWord(01000, false, 0x0201);
    preview_sprite::Image image(false);
    const preview_sprite::Selection mono{01000, 1, 0}, color{01000, 1, 1};
    ASSERT_TRUE(image.Update(board, mono));
    const auto original = image.Get(mono);
    ASSERT_TRUE(original);
    EXPECT_EQ(original->pixels.front(), 0xffffffffu);
    EXPECT_FALSE(image.Get(color));
    const auto revision = original->revision;
    ASSERT_TRUE(image.Update(board, mono));
    ASSERT_TRUE(image.Get(mono));
    EXPECT_EQ(image.Get(mono)->revision, revision);
    ASSERT_TRUE(image.Update(board, color));
    ASSERT_TRUE(image.Get(color));
    EXPECT_NE(image.Get(color)->revision, revision);
    EXPECT_EQ(image.Get(color)->pixels.front(), 0xff00ff00u);
    EXPECT_FALSE(image.Get(mono));

    const preview_sprite::Selection odd{01001, 1, 0};
    ASSERT_TRUE(image.Update(board, odd));
    ASSERT_TRUE(image.Get(odd));
    const auto oddRevision = image.Get(odd)->revision;
    memory->SetByte(01000, false, 0xa5);
    memory->SetByte(static_cast<std::uint16_t>(odd.address + sprite::PageBytes(odd.width, odd.mode)), false, 0x71);
    ASSERT_TRUE(image.Update(board, odd));
    ASSERT_TRUE(image.Get(odd));
    EXPECT_EQ(image.Get(odd)->revision, oddRevision);

    const preview_sprite::Selection wrapped{0xffff, 3, 2};
    std::vector<std::uint32_t> reference(sprite::Width * sprite::Height);
    ASSERT_TRUE(sprite::Decode(reference, wrapped.address, wrapped.width, wrapped.mode, [&](std::uint16_t at) {
        return memory->GetWordView(at, cpu->IsHaltMode(), false).value;
    }));
    ASSERT_TRUE(image.Update(board, wrapped));
    const auto actual = image.Get(wrapped);
    ASSERT_TRUE(actual);
    for (int row = 0; row < sprite::Height; ++row)
        for (int x = 0; x < sprite::Width; ++x)
            ASSERT_EQ(actual->pixels[row * sprite::Width + x],
                reference[(sprite::Height - 1 - row) * sprite::Width + x] | 0xff000000u);
}

TEST_F(SpriteImageTest, WorkerUsesTheCapturedPageAndPublishesLaterChanges)
{
    CMotherboard board;
    board.Reset();
    board.GetCPU()->SetPSW(0);
    auto* memory = board.GetCPU()->GetMemoryController();
    memory->SetWord(01000, false, 1);
    preview_sprite::Image image;
    const preview_sprite::Selection selection{01000, 1, 0};
    ASSERT_TRUE(image.Update(board, selection));
    auto first = image.Get(selection);
    memory->SetWord(01000, false, 2);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!first && std::chrono::steady_clock::now() < deadline)
    {
        ASSERT_TRUE(image.Update(board, selection));
        first = image.Get(selection);
        std::this_thread::yield();
    }
    ASSERT_TRUE(first);
    EXPECT_EQ(first->pixels[0], 0xffffffffu);
    EXPECT_EQ(first->pixels[1], 0xff000000u);
    const auto firstRevision = first->revision;
    auto latest = first;
    while (latest->revision == firstRevision && std::chrono::steady_clock::now() < deadline)
    {
        ASSERT_TRUE(image.Update(board, selection));
        latest = image.Get(selection);
        ASSERT_TRUE(latest);
        std::this_thread::yield();
    }
    EXPECT_NE(latest->revision, firstRevision);
    EXPECT_EQ(latest->pixels[0], 0xff000000u);
    EXPECT_EQ(latest->pixels[1], 0xffffffffu);
}
