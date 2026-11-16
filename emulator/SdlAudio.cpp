/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "SdlAudio.h"
#include "FramePacing.h"
#include "emubase/Board.h"
#include "emubase/Emubase.h"

static_assert(sdl_audio::SampleRate == SAMPLERATE && sdl_audio::SamplesPerFrame == SAMPLERATE / FRAMERATE);

sdl_audio::Output::Output(CMotherboard& board, SDL_AudioStream& stream) : board_(board), stream_(stream)
{
    board_.SetSoundGenCallbackWithContext(Feed, this);
}

sdl_audio::Output::~Output() { board_.SetSoundGenCallbackWithContext(nullptr, nullptr); }

void sdl_audio::Output::Feed(void* context, unsigned short left, unsigned short right)
{
    auto& output = *static_cast<Output*>(context);
    (void)output.batch_.Push(left, right);
}

bool sdl_audio::Output::SubmitFrame()
{
    const int queued = SDL_GetAudioStreamQueued(&stream_);
    if (queued < 0) return false;
    if (queued > MaxQueuedBytes - FrameBytes && !SDL_ClearAudioStream(&stream_)) return false;
    const auto samples = batch_.Bytes();
    const bool success = samples.empty() || SDL_PutAudioStreamData(&stream_, samples.data(), static_cast<int>(samples.size()));
    batch_.Reset();
    return success;
}

bool sdl_audio::Output::Reset()
{
    batch_.Reset();
    return SDL_ClearAudioStream(&stream_);
}

bool sdl_audio::Output::Configure(std::uint16_t speed, int volume)
{
    if (!frame_pacing::IsSupportedSpeed(speed) || volume < 0 || volume > 100) return false;
    return SDL_SetAudioStreamFrequencyRatio(&stream_, frame_pacing::AudioRatio(speed)) &&
        SDL_SetAudioStreamGain(&stream_, static_cast<float>(volume) / 100.0f);
}
