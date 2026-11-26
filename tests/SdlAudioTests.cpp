/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "SdlAudio.h"
#include "FrameWorker.h"
#include "AsyncScreenshot.h"
#include "VideoDecoder.h"
#include "emubase/Emubase.h"
#include <SDL3/SDL.h>
#include <gtest/gtest.h>
#include <array>
#include <chrono>
#include <filesystem>
#include <memory>
#include <utility>

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

TEST_F(SdlAudioTest, WorkerFramesHandOffCompleteAudioBatches)
{
    sdl_audio::Output output(*board, *stream);
    board->SetSound(0200);
    FrameWorker worker([&] { return board->SystemFrame(); });
    for (int frame = 0; frame < 24; ++frame)
    {
        ASSERT_TRUE(worker.Start());
        ASSERT_TRUE(worker.Wait());
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
        ASSERT_TRUE(output.Reset());
    }
}

TEST_F(SdlAudioTest, BlockedSubmissionDropsNewBatchAndPreservesOwnedPcm)
{
    sdl_audio::FrameQueue queue;
    sdl_audio::Output output(*board, *stream);
    auto submitted = sdl_audio::FrameQueue::Result::failed;
    FrameWorker worker([&] {
        if (!board->SystemFrame()) return false;
        submitted = queue.Submit(*stream, output.TakeFrame());
        return submitted != sdl_audio::FrameQueue::Result::failed;
    });
    board->SetSound(0200);
    {
        ASSERT_TRUE(SDL_LockAudioStream(stream.get()));
        struct Unlock
        {
            SDL_AudioStream* stream;
            ~Unlock() { SDL_UnlockAudioStream(stream); }
        } unlock{stream.get()};
        ASSERT_TRUE(worker.Start());
        ASSERT_TRUE(worker.WaitFor(std::chrono::seconds(5)));
        ASSERT_TRUE(worker.Wait());
        EXPECT_EQ(submitted, sdl_audio::FrameQueue::Result::queued);
        EXPECT_TRUE(queue.Check());
        board->SetSound(0);
        ASSERT_TRUE(worker.Start());
        ASSERT_TRUE(worker.WaitFor(std::chrono::seconds(5)));
        ASSERT_TRUE(worker.Wait());
        EXPECT_EQ(submitted, sdl_audio::FrameQueue::Result::dropped);
    }
    ASSERT_TRUE(queue.Drain());
    ASSERT_EQ(SDL_GetAudioStreamQueued(stream.get()), sdl_audio::FrameBytes);
    ASSERT_TRUE(SDL_FlushAudioStream(stream.get()));
    std::array<std::uint8_t, sdl_audio::FrameBytes> bytes{};
    ASSERT_EQ(SDL_GetAudioStreamData(stream.get(), bytes.data(), static_cast<int>(bytes.size())), bytes.size());
    for (std::size_t i = 0; i < bytes.size(); i += 2)
    {
        EXPECT_EQ(bytes[i], 0xff);
        EXPECT_EQ(bytes[i + 1], 0x7f);
    }
    ASSERT_TRUE(worker.Start());
    ASSERT_TRUE(worker.Wait());
    EXPECT_EQ(submitted, sdl_audio::FrameQueue::Result::queued);
    EXPECT_TRUE(queue.Drain());
}

TEST_F(SdlAudioTest, QueueReportsAsyncFailureWhenUnboundStreamCannotResume)
{
    sdl_audio::FrameQueue queue;
    sdl_audio::PcmFrame frame;
    (void)frame.Push(0, 0);
    ASSERT_EQ(queue.Submit(*stream, frame, true), sdl_audio::FrameQueue::Result::queued);
    EXPECT_FALSE(queue.Drain());
    EXPECT_FALSE(queue.Check());
    EXPECT_EQ(queue.Submit(*stream, frame), sdl_audio::FrameQueue::Result::failed);
}

TEST_F(SdlAudioTest, FrameAudioAndScreenshotWorkersCanRunTogether)
{
    const auto path = std::filesystem::temp_directory_path() /
        ("ukncbtl-concurrent-screenshot-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".png");
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code error; std::filesystem::remove(path, error); } } cleanup{path};
    sdl_audio::Output output(*board, *stream);
    board->SetSound(0200);
    screenshot_file::AsyncSave screenshot;
    sdl_audio::FrameQueue queue;
    std::vector<std::uint32_t> pixels(video::Width * video::Height, 0x123456);
    ASSERT_TRUE(screenshot.StartScaled(path, std::move(pixels), 3));
    FrameWorker worker([&] {
        return board->SystemFrame() && queue.Submit(*stream, output.TakeFrame()) == sdl_audio::FrameQueue::Result::queued;
    });
    for (int frame = 0; frame < 12; ++frame)
    {
        ASSERT_TRUE(worker.Start());
        ASSERT_TRUE(worker.Wait());
        ASSERT_TRUE(queue.Drain());
        EXPECT_EQ(SDL_GetAudioStreamQueued(stream.get()), sdl_audio::FrameBytes);
        ASSERT_TRUE(output.Reset());
    }
    ASSERT_EQ(screenshot.Finish(), std::optional<bool>(true));
    const auto utf8 = path.u8string();
    const std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> image(
        SDL_LoadPNG(reinterpret_cast<const char*>(utf8.c_str())), SDL_DestroySurface);
    ASSERT_TRUE(image) << SDL_GetError();
    EXPECT_EQ(image->w, 640);
    EXPECT_EQ(image->h, 576);
    Uint8 red = 0, green = 0, blue = 0, alpha = 0;
    ASSERT_TRUE(SDL_ReadSurfacePixel(image.get(), 0, 0, &red, &green, &blue, &alpha));
    EXPECT_EQ(red, 0x12); EXPECT_EQ(green, 0x34); EXPECT_EQ(blue, 0x56); EXPECT_EQ(alpha, 0xff);
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

TEST_F(SdlAudioTest, ConfiguresSpeedAndVolumeWithoutChangingInvalidValues)
{
    sdl_audio::Output output(*board, *stream);
    for (const auto [speed, ratio] : {std::pair{0, 5.0f}, std::pair{1, 1.0f}, std::pair{2, 2.0f},
        std::pair{3, 4.0f}, std::pair{0x7fff, 0.5f}, std::pair{0x7ffe, 0.25f}, std::pair{0x7ffd, 0.1f}})
    {
        ASSERT_TRUE(output.Configure(static_cast<std::uint16_t>(speed), 35));
        EXPECT_FLOAT_EQ(SDL_GetAudioStreamFrequencyRatio(stream.get()), ratio);
        EXPECT_FLOAT_EQ(SDL_GetAudioStreamGain(stream.get()), 0.35f);
    }
    EXPECT_FALSE(output.Configure(4, 50));
    EXPECT_FALSE(output.Configure(1, -1));
    EXPECT_FALSE(output.Configure(1, 101));
    EXPECT_FLOAT_EQ(SDL_GetAudioStreamFrequencyRatio(stream.get()), 0.1f);
    EXPECT_FLOAT_EQ(SDL_GetAudioStreamGain(stream.get()), 0.35f);
}

TEST(SdlAudioPlayback, ReopensTransactionallyAndPreservesCallbackOwnership)
{
    ASSERT_TRUE(SDL_SetHintWithPriority(SDL_HINT_AUDIO_DRIVER, "dummy", SDL_HINT_OVERRIDE));
    ASSERT_TRUE(SDL_InitSubSystem(SDL_INIT_AUDIO)) << SDL_GetError();
    struct AudioGuard { ~AudioGuard() { SDL_QuitSubSystem(SDL_INIT_AUDIO); } } audioGuard;
    CProcessor::Init();
    struct ProcessorGuard { ~ProcessorGuard() { CProcessor::Done(); } } processorGuard;
    CMotherboard board;
    board.Reset();
    board.GetCPU()->SetDCLOPin(true); board.GetPPU()->SetDCLOPin(true);
    sdl_audio::Playback playback(board);
    EXPECT_FALSE(playback.IsOpen());
    ASSERT_TRUE(playback.Open(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, 1, 50, false)) << SDL_GetError();
    EXPECT_FALSE(playback.Active());
    EXPECT_TRUE(playback.SetActive(true));
    EXPECT_TRUE(playback.Active());
    EXPECT_TRUE(SDL_AudioDevicePaused(playback.LogicalDevice()));
    for (int i = 0; i < 2; ++i)
    {
        ASSERT_TRUE(board.SystemFrame());
        ASSERT_TRUE(playback.SubmitFrame());
        ASSERT_TRUE(playback.DrainSubmission());
        EXPECT_TRUE(SDL_AudioDevicePaused(playback.LogicalDevice()));
    }
    ASSERT_TRUE(board.SystemFrame());
    ASSERT_TRUE(playback.SubmitFrame());
    ASSERT_TRUE(playback.DrainSubmission());
    EXPECT_FALSE(SDL_AudioDevicePaused(playback.LogicalDevice()));
    ASSERT_TRUE(playback.Reset());
    EXPECT_TRUE(SDL_AudioDevicePaused(playback.LogicalDevice()));
    for (int i = 0; i < 3; ++i)
    {
        ASSERT_TRUE(board.SystemFrame());
        ASSERT_TRUE(playback.SubmitFrame());
        if (i < 2) ASSERT_TRUE(playback.DrainSubmission());
    }
    ASSERT_TRUE(playback.SetActive(false));
    EXPECT_TRUE(SDL_AudioDevicePaused(playback.LogicalDevice()));
    ASSERT_TRUE(playback.SetActive(true));
    EXPECT_FALSE(playback.Open(0, 1, 50, true));
    EXPECT_FALSE(playback.Open(static_cast<SDL_AudioDeviceID>(0x7ffffffe), 1, 50, true));
    EXPECT_FALSE(playback.Open(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, 4, 50, true));
    EXPECT_TRUE(playback.IsOpen());
    EXPECT_TRUE(playback.Active());
    ASSERT_TRUE(board.SystemFrame());
    EXPECT_TRUE(playback.SubmitFrame());
    ASSERT_TRUE(playback.Open(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, 2, 25, false)) << SDL_GetError();
    EXPECT_FALSE(playback.Active());
    ASSERT_TRUE(board.SystemFrame());
    EXPECT_TRUE(playback.SubmitFrame());
    EXPECT_TRUE(playback.Reset());
    for (const auto& device : sdl_audio::PlaybackDevices())
    { EXPECT_NE(device.id, 0u); EXPECT_FALSE(device.name.empty()); }
    playback.Close();
    EXPECT_FALSE(playback.IsOpen());
    EXPECT_FALSE(playback.Active());
    EXPECT_FALSE(playback.SetActive(true));
    EXPECT_FALSE(playback.SubmitFrame());
}

TEST(SdlAudioPlayback, HandlesSelectedAndLogicalDeviceRemoval)
{
    ASSERT_TRUE(SDL_SetHintWithPriority(SDL_HINT_AUDIO_DRIVER, "dummy", SDL_HINT_OVERRIDE));
    ASSERT_TRUE(SDL_InitSubSystem(SDL_INIT_AUDIO)) << SDL_GetError();
    struct AudioGuard { ~AudioGuard() { SDL_QuitSubSystem(SDL_INIT_AUDIO); } } audioGuard;
    CProcessor::Init();
    struct ProcessorGuard { ~ProcessorGuard() { CProcessor::Done(); } } processorGuard;
    CMotherboard board;
    board.Reset();
    board.GetCPU()->SetDCLOPin(true); board.GetPPU()->SetDCLOPin(true);
    sdl_audio::Playback playback(board);
    FrameWorker worker([&] { return board.SystemFrame() && playback.SubmitFrame(); });
    using enum sdl_audio::RemovalResult;
    EXPECT_EQ(playback.HandleDeviceRemoval(1, false, 1, 50), ignored);
    ASSERT_TRUE(playback.Open(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, 1, 50, true));
    const auto logical = playback.LogicalDevice();
    ASSERT_NE(logical, 0u);
    EXPECT_EQ(playback.HandleDeviceRemoval(logical, true, 1, 50), ignored);
    EXPECT_EQ(playback.HandleDeviceRemoval(0, false, 1, 50), ignored);
    const auto devices = sdl_audio::PlaybackDevices();
    ASSERT_FALSE(devices.empty());
    EXPECT_EQ(playback.HandleDeviceRemoval(devices.front().id, false, 1, 50), ignored);
    EXPECT_EQ(playback.HandleDeviceRemoval(logical, false, 1, 50), switchedToDefault);
    EXPECT_EQ(playback.Device(), SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK);
    EXPECT_TRUE(playback.Active());
    ASSERT_TRUE(worker.Start());
    ASSERT_TRUE(worker.Wait());
    ASSERT_TRUE(worker.Start([&] { return playback.Open(devices.front().id, 2, 25, false); }));
    ASSERT_TRUE(worker.Wait());
    ASSERT_TRUE(worker.Start([&] {
        return playback.HandleDeviceRemoval(devices.front().id, false, 2, 25) == switchedToDefault;
    }));
    ASSERT_TRUE(worker.Wait());
    EXPECT_EQ(playback.Device(), SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK);
    EXPECT_FALSE(playback.Active());
    EXPECT_EQ(playback.HandleDeviceRemoval(playback.LogicalDevice(), false, 4, 25), unavailable);
    EXPECT_FALSE(playback.IsOpen());
    EXPECT_FALSE(playback.Active());
}

TEST(SdlAudioPlayback, BoardWorkerSubmitsAndReprimesAroundDeviceControls)
{
    ASSERT_TRUE(SDL_SetHintWithPriority(SDL_HINT_AUDIO_DRIVER, "dummy", SDL_HINT_OVERRIDE));
    ASSERT_TRUE(SDL_InitSubSystem(SDL_INIT_AUDIO)) << SDL_GetError();
    struct AudioGuard { ~AudioGuard() { SDL_QuitSubSystem(SDL_INIT_AUDIO); } } audioGuard;
    CProcessor::Init();
    struct ProcessorGuard { ~ProcessorGuard() { CProcessor::Done(); } } processorGuard;
    CMotherboard board;
    board.Reset();
    board.GetCPU()->SetDCLOPin(true); board.GetPPU()->SetDCLOPin(true);
    sdl_audio::Playback playback(board);
    FrameWorker worker([&] { return board.SystemFrame() && playback.SubmitFrame(); });
    ASSERT_TRUE(worker.Start([&] {
        return playback.Open(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, 1, 50, true);
    }));
    ASSERT_TRUE(worker.Wait());
    const auto device = playback.LogicalDevice();
    ASSERT_NE(device, 0u);
    ASSERT_TRUE(worker.Start());
    ASSERT_TRUE(worker.Wait());
    // Reset and reconfigure while the first audio batch can still be pending.
    ASSERT_TRUE(worker.Start([&] { return playback.Reset() && playback.Configure(2, 30); }));
    ASSERT_TRUE(worker.Wait());
    EXPECT_TRUE(playback.Active());
    EXPECT_EQ(playback.LogicalDevice(), device);
    EXPECT_TRUE(SDL_AudioDevicePaused(device));
    for (int frame = 1; frame <= 3; ++frame)
    {
        ASSERT_TRUE(worker.Start());
        ASSERT_TRUE(worker.Wait());
        ASSERT_TRUE(playback.DrainSubmission());
        EXPECT_EQ(SDL_AudioDevicePaused(device), frame < 3);
    }
    ASSERT_TRUE(worker.Start([&] { return playback.SetActive(false); }));
    ASSERT_TRUE(worker.Wait());
    EXPECT_FALSE(playback.Active());
    EXPECT_TRUE(SDL_AudioDevicePaused(device));
    ASSERT_TRUE(worker.Start([&] { playback.Close(); return true; }));
    ASSERT_TRUE(worker.Wait());
    EXPECT_FALSE(playback.IsOpen());
}

TEST_F(SdlAudioTest, DetachedOpenCanOverlapGuestFramesAndInstallsPausedWithCurrentSettings)
{
    ASSERT_TRUE(SDL_SetHintWithPriority(SDL_HINT_AUDIO_DRIVER, "dummy", SDL_HINT_OVERRIDE));
    ASSERT_TRUE(SDL_InitSubSystem(SDL_INIT_AUDIO)) << SDL_GetError();
    struct AudioGuard { ~AudioGuard() { SDL_QuitSubSystem(SDL_INIT_AUDIO); } } audioGuard;
    sdl_audio::Playback playback(*board);
    ASSERT_TRUE(playback.Open(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, 1, 90, true));
    const auto original = playback.LogicalDevice();
    ASSERT_NE(0u, original);

    sdl_audio::AsyncDeviceOpen opening;
    FrameWorker guest([&] { return board->SystemFrame() && playback.SubmitFrame(); });
    ASSERT_TRUE(opening.Start(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, 1, 90));
    EXPECT_FALSE(opening.Start(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, 1, 90));
    for (int frame = 0; frame < 4; ++frame)
    {
        ASSERT_TRUE(guest.Start());
        ASSERT_EQ(guest.WaitOutcome(), FrameWorker::Outcome::completed);
        ASSERT_TRUE(playback.DrainSubmission());
        EXPECT_EQ(playback.LogicalDevice(), original);
    }

    ASSERT_TRUE(opening.WaitFor(std::chrono::seconds(5)));
    auto candidate = opening.Poll();
    ASSERT_TRUE(candidate && candidate->prepared);
    const auto replacement = candidate->prepared->LogicalDevice();
    ASSERT_NE(0u, replacement);
    EXPECT_NE(original, replacement);
    EXPECT_TRUE(SDL_AudioDevicePaused(replacement));
    ASSERT_TRUE(guest.Start([&] { return playback.Install(std::move(candidate->prepared), 2, 35, true); }));
    ASSERT_EQ(guest.WaitOutcome(), FrameWorker::Outcome::completed);
    EXPECT_EQ(playback.LogicalDevice(), replacement);
    for (int frame = 1; frame <= 3; ++frame)
    {
        ASSERT_TRUE(guest.Start());
        ASSERT_EQ(guest.WaitOutcome(), FrameWorker::Outcome::completed);
        ASSERT_TRUE(playback.DrainSubmission());
        EXPECT_EQ(SDL_AudioDevicePaused(replacement), frame < 3);
    }
}

TEST_F(SdlAudioTest, DisconnectedOrFailedCandidateLeavesTheCurrentDeviceAvailable)
{
    ASSERT_TRUE(SDL_SetHintWithPriority(SDL_HINT_AUDIO_DRIVER, "dummy", SDL_HINT_OVERRIDE));
    ASSERT_TRUE(SDL_InitSubSystem(SDL_INIT_AUDIO)) << SDL_GetError();
    struct AudioGuard { ~AudioGuard() { SDL_QuitSubSystem(SDL_INIT_AUDIO); } } audioGuard;
    sdl_audio::Playback playback(*board);
    ASSERT_TRUE(playback.Open(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, 1, 50, true));
    const auto original = playback.LogicalDevice();
    const auto devices = sdl_audio::PlaybackDevices();
    ASSERT_FALSE(devices.empty());

    sdl_audio::AsyncDeviceOpen opening;
    ASSERT_TRUE(opening.Start(devices.front().id, 1, 50));
    ASSERT_TRUE(opening.WaitFor(std::chrono::seconds(5)));
    opening.DeviceRemoved(devices.front().id);
    auto candidate = opening.Finish();
    ASSERT_TRUE(candidate);
    EXPECT_FALSE(candidate->prepared);
    EXPECT_EQ(candidate->error, "Audio device disconnected while opening");
    EXPECT_EQ(playback.LogicalDevice(), original);

    ASSERT_TRUE(opening.Start(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, 4, 50));
    candidate = opening.Finish();
    ASSERT_TRUE(candidate);
    EXPECT_FALSE(candidate->prepared);
    EXPECT_FALSE(candidate->error.empty());
    EXPECT_EQ(playback.LogicalDevice(), original);

    ASSERT_TRUE(opening.Start(devices.front().id, 1, 50));
    candidate = opening.Finish();
    ASSERT_TRUE(candidate && candidate->prepared);
    EXPECT_FALSE(playback.Install(std::move(candidate->prepared), 1, 101, true));
    EXPECT_EQ(playback.LogicalDevice(), original);
    EXPECT_TRUE(playback.Active());
    ASSERT_TRUE(board->SystemFrame());
    EXPECT_TRUE(playback.SubmitFrame());
    EXPECT_TRUE(playback.DrainSubmission());
}
}
