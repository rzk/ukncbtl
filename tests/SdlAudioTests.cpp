/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "SdlAudio.h"
#include "emubase/Emubase.h"
#include <gtest/gtest.h>
#include <array>
#include <memory>

void DebugLog(const wchar_t*) {}

namespace
{
int legacySamples = 0;
void LegacySample(unsigned short, unsigned short) { ++legacySamples; }
void ContextSample(void* context, unsigned short, unsigned short) { ++*static_cast<int*>(context); }

class SdlAudioTest : public testing::Test
{
protected:
    std::unique_ptr<CMotherboard> board;
    std::unique_ptr<SDL_AudioStream, decltype(&SDL_DestroyAudioStream)> stream{nullptr, SDL_DestroyAudioStream};
    void SetUp() override
    {
        CProcessor::Init();
        board = std::make_unique<CMotherboard>();
        board->Reset();
        board->GetCPU()->SetDCLOPin(true);
        board->GetPPU()->SetDCLOPin(true);
        const SDL_AudioSpec spec{SDL_AUDIO_S16LE, sdl_audio::Channels, sdl_audio::SampleRate};
        stream.reset(SDL_CreateAudioStream(&spec, &spec));
        ASSERT_TRUE(stream) << SDL_GetError();
    }
    void TearDown() override { stream.reset(); board.reset(); CProcessor::Done(); }
};

TEST_F(SdlAudioTest, SubmitsInterleavedPcmAndDetachesOnDestruction)
{
    {
        sdl_audio::Output output(*board, *stream);
        board->SetSound(0200);
        ASSERT_TRUE(board->SystemFrame());
        ASSERT_TRUE(output.SubmitFrame());
        EXPECT_EQ(SDL_GetAudioStreamQueued(stream.get()), sdl_audio::FrameBytes);
        ASSERT_TRUE(SDL_FlushAudioStream(stream.get()));
        std::array<std::uint8_t, sdl_audio::FrameBytes> bytes{};
        ASSERT_EQ(SDL_GetAudioStreamData(stream.get(), bytes.data(), static_cast<int>(bytes.size())), bytes.size());
        for (std::size_t i = 0; i < bytes.size(); i += 4)
        {
            EXPECT_EQ(bytes[i], 0xff); EXPECT_EQ(bytes[i + 1], 0x7f);
            EXPECT_EQ(bytes[i + 2], 0xff); EXPECT_EQ(bytes[i + 3], 0x7f);
        }
    }
    ASSERT_TRUE(SDL_ClearAudioStream(stream.get()));
    ASSERT_TRUE(board->SystemFrame());
    EXPECT_EQ(SDL_GetAudioStreamQueued(stream.get()), 0);
}

TEST_F(SdlAudioTest, BoundsBacklogAndResetDiscardsSamples)
{
    sdl_audio::Output output(*board, *stream);
    for (int i = 0; i < 15; ++i)
    {
        ASSERT_TRUE(board->SystemFrame());
        ASSERT_TRUE(output.SubmitFrame());
        EXPECT_LE(SDL_GetAudioStreamQueued(stream.get()), sdl_audio::MaxQueuedBytes);
    }
    ASSERT_TRUE(board->SystemFrame());
    ASSERT_TRUE(output.Reset());
    EXPECT_EQ(SDL_GetAudioStreamQueued(stream.get()), 0);
    ASSERT_TRUE(output.SubmitFrame());
    EXPECT_EQ(SDL_GetAudioStreamQueued(stream.get()), 0);
}

TEST_F(SdlAudioTest, LegacyAndContextCallbacksReplaceEachOther)
{
    int contextSamples = 0;
    legacySamples = 0;
    board->SetSoundGenCallbackWithContext(ContextSample, &contextSamples);
    ASSERT_TRUE(board->SystemFrame());
    EXPECT_EQ(contextSamples, sdl_audio::SamplesPerFrame);
    board->SetSoundGenCallback(LegacySample);
    ASSERT_TRUE(board->SystemFrame());
    EXPECT_EQ(contextSamples, sdl_audio::SamplesPerFrame);
    EXPECT_EQ(legacySamples, sdl_audio::SamplesPerFrame);
    board->SetSoundGenCallbackWithContext(ContextSample, &contextSamples);
    ASSERT_TRUE(board->SystemFrame());
    EXPECT_EQ(contextSamples, 2 * sdl_audio::SamplesPerFrame);
    EXPECT_EQ(legacySamples, sdl_audio::SamplesPerFrame);
    board->SetSoundGenCallbackWithContext(nullptr, nullptr);
    ASSERT_TRUE(board->SystemFrame());
    EXPECT_EQ(contextSamples, 2 * sdl_audio::SamplesPerFrame);
}
}
