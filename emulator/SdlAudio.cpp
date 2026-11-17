/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "SdlAudio.h"
#include "FramePacing.h"
#include "emubase/Board.h"
#include "emubase/Emubase.h"
#include <SDL3/SDL_stdinc.h>
#include <utility>

static_assert(sdl_audio::SampleRate == SAMPLERATE && sdl_audio::SamplesPerFrame == SAMPLERATE / FRAMERATE);

namespace
{
bool ConfigureStream(SDL_AudioStream& stream, std::uint16_t speed, int volume)
{
    if (!frame_pacing::IsSupportedSpeed(speed) || volume < 0 || volume > 100) return false;
    return SDL_SetAudioStreamFrequencyRatio(&stream, frame_pacing::AudioRatio(speed)) &&
        SDL_SetAudioStreamGain(&stream, static_cast<float>(volume) / 100.0f);
}
}

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
    return ConfigureStream(stream_, speed, volume);
}

std::vector<sdl_audio::DeviceInfo> sdl_audio::PlaybackDevices()
{
    int count = 0;
    const std::unique_ptr<SDL_AudioDeviceID, decltype(&SDL_free)> devices(SDL_GetAudioPlaybackDevices(&count), SDL_free);
    std::vector<DeviceInfo> result;
    for (int index = 0; devices && index < count; ++index)
    {
        const auto id = devices.get()[index];
        const char* name = SDL_GetAudioDeviceName(id);
        result.push_back({id, name ? name : "Unknown device"});
    }
    return result;
}

bool sdl_audio::Playback::Open(SDL_AudioDeviceID device, std::uint16_t speed, int volume, bool active)
{
    if (!device || !frame_pacing::IsSupportedSpeed(speed) || volume < 0 || volume > 100) return false;
    const SDL_AudioSpec spec{SDL_AUDIO_S16LE, Channels, SampleRate};
    std::unique_ptr<SDL_AudioStream, decltype(&SDL_DestroyAudioStream)> candidate(
        SDL_OpenAudioDeviceStream(device, &spec, nullptr, nullptr), SDL_DestroyAudioStream);
    if (!candidate || !ConfigureStream(*candidate, speed, volume) ||
        (active && !SDL_ResumeAudioStreamDevice(candidate.get()))) return false;
    // The old adapter must detach before a new one installs its board callback.
    output_.reset();
    stream_ = std::move(candidate);
    output_ = std::make_unique<Output>(board_, *stream_);
    device_ = device;
    active_ = active;
    return true;
}

void sdl_audio::Playback::Close()
{
    output_.reset();
    stream_.reset();
    active_ = false;
}

SDL_AudioDeviceID sdl_audio::Playback::LogicalDevice() const
{ return stream_ ? SDL_GetAudioStreamDevice(stream_.get()) : 0; }

sdl_audio::RemovalResult sdl_audio::Playback::HandleDeviceRemoval(SDL_AudioDeviceID removed, bool recording,
    std::uint16_t speed, int volume)
{
    if (!IsOpen() || recording || !removed) return RemovalResult::ignored;
    const auto logical = LogicalDevice();
    if (removed != logical && (device_ == SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK || removed != device_))
        return RemovalResult::ignored;
    const bool wasActive = active_;
    if (Open(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, speed, volume, wasActive)) return RemovalResult::switchedToDefault;
    Close();
    return RemovalResult::unavailable;
}

bool sdl_audio::Playback::SetActive(bool active)
{
    if (!output_) return false;
    if (active_ == active) return true;
    if (!SDL_PauseAudioStreamDevice(stream_.get()) || !output_->Reset() ||
        (active && !SDL_ResumeAudioStreamDevice(stream_.get()))) return false;
    active_ = active;
    return true;
}

bool sdl_audio::Playback::Configure(std::uint16_t speed, int volume)
{ return output_ && output_->Configure(speed, volume); }
bool sdl_audio::Playback::Reset() { return output_ && output_->Reset(); }
bool sdl_audio::Playback::SubmitFrame() { return output_ && output_->SubmitFrame(); }
