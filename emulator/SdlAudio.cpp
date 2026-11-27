/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "SdlAudio.h"
#include "FramePacing.h"
#include "FrameWorker.h"
#include "emubase/Board.h"
#include "emubase/Emubase.h"
#include "util/Profiler.h"
#include "util/SdlThreadCleanup.h"
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_stdinc.h>
#include <algorithm>
#include <new>
#include <system_error>
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

bool SubmitSamples(SDL_AudioStream& stream, std::span<const char> samples)
{
    UKNC_PROFILE_SCOPE("Audio submit");
    const int queued = SDL_GetAudioStreamQueued(&stream);
    if (queued < 0) return false;
    if (queued > sdl_audio::MaxQueuedBytes - sdl_audio::FrameBytes && !SDL_ClearAudioStream(&stream)) return false;
    return samples.empty() || SDL_PutAudioStreamData(&stream, samples.data(), static_cast<int>(samples.size()));
}
}

struct sdl_audio::FrameQueue::Worker
{
    bool Collect(bool wait)
    {
        if (!pending || (!wait && !thread.WaitFor(std::chrono::milliseconds(0)))) return true;
        const auto outcome = thread.WaitOutcome();
        pending = false;
        return outcome == FrameWorker::Outcome::completed;
    }

    SDL_AudioStream* stream = nullptr; // Borrowed while pending.
    PcmFrame frame;
    bool resume = false;
    bool pending = false;
    bool failed = false;
    FrameWorker thread{[this] {
        thread_local const SdlThreadCleanup cleanup;
        return SubmitSamples(*stream, frame.Bytes()) && (!resume || SDL_ResumeAudioStreamDevice(stream));
    }};
};

sdl_audio::FrameQueue::FrameQueue() : worker_(std::make_unique<Worker>()) {}
sdl_audio::FrameQueue::~FrameQueue() = default;

sdl_audio::FrameQueue::Result sdl_audio::FrameQueue::Submit(SDL_AudioStream& stream, PcmFrame frame, bool resume)
{
    if (!Check()) return Result::failed;
    if (worker_->pending) return Result::dropped;
    worker_->stream = &stream;
    worker_->frame = std::move(frame);
    worker_->resume = resume;
    worker_->pending = worker_->thread.Start();
    worker_->failed = !worker_->pending;
    return worker_->pending ? Result::queued : Result::failed;
}

bool sdl_audio::FrameQueue::Check()
{
    if (!worker_->Collect(false)) worker_->failed = true;
    return !worker_->failed;
}

bool sdl_audio::FrameQueue::Drain()
{
    if (!worker_->Collect(true)) worker_->failed = true;
    return !worker_->failed;
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
    return SubmitSamples(stream_, TakeFrame().Bytes());
}

sdl_audio::PcmFrame sdl_audio::Output::TakeFrame()
{
    auto frame = batch_;
    batch_.Reset();
    return frame;
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

SDL_AudioDeviceID sdl_audio::PreparedDevice::LogicalDevice() const
{
    return stream_ ? SDL_GetAudioStreamDevice(stream_.get()) : 0;
}

std::unique_ptr<sdl_audio::PreparedDevice> sdl_audio::Playback::Prepare(
    SDL_AudioDeviceID device, std::uint16_t speed, int volume)
{
    UKNC_PROFILE_SCOPE("Open audio device");
    if (!device || !frame_pacing::IsSupportedSpeed(speed) || volume < 0 || volume > 100)
    {
        SDL_SetError("Invalid audio device, speed, or volume");
        return {};
    }
    const SDL_AudioSpec spec{SDL_AUDIO_S16LE, Channels, SampleRate};
    std::unique_ptr<SDL_AudioStream, decltype(&SDL_DestroyAudioStream)> candidate(
        SDL_OpenAudioDeviceStream(device, &spec, nullptr, nullptr), SDL_DestroyAudioStream);
    if (!candidate || !ConfigureStream(*candidate, speed, volume)) return {};
    try
    {
        auto prepared = std::make_unique<PreparedDevice>();
        prepared->queue_ = std::make_unique<FrameQueue>();
        prepared->stream_ = std::move(candidate);
        prepared->device_ = device;
        prepared->speed_ = speed;
        prepared->volume_ = volume;
        return prepared;
    }
    catch (const std::system_error&) { SDL_SetError("Could not start the audio submission worker"); }
    catch (const std::bad_alloc&) { SDL_OutOfMemory(); }
    return {};
}

bool sdl_audio::Playback::Install(std::unique_ptr<PreparedDevice> prepared, std::uint16_t speed, int volume, bool active)
{
    if (!prepared || !prepared->LogicalDevice() || !prepared->queue_) return false;
    if ((prepared->speed_ != speed || prepared->volume_ != volume) &&
        !ConfigureStream(*prepared->stream_, speed, volume)) return false;
    // The old adapter must detach before a new one installs its board callback.
    queue_.reset();
    output_.reset();
    stream_ = std::move(prepared->stream_);
    output_.emplace(board_, *stream_);
    queue_ = std::move(prepared->queue_);
    device_ = prepared->device_;
    active_ = active;
    primingFrames_ = active ? 3 : 0;
    return true;
}

bool sdl_audio::Playback::Open(SDL_AudioDeviceID device, std::uint16_t speed, int volume, bool active)
{
    return Install(Prepare(device, speed, volume), speed, volume, active);
}

void sdl_audio::Playback::Close()
{
    queue_.reset();
    output_.reset();
    stream_.reset();
    active_ = false;
    primingFrames_ = 0;
}

SDL_AudioDeviceID sdl_audio::Playback::LogicalDevice() const
{ return stream_ ? SDL_GetAudioStreamDevice(stream_.get()) : 0; }

bool sdl_audio::Playback::MatchesRemoval(SDL_AudioDeviceID removed, bool recording) const
{
    return IsOpen() && !recording && removed &&
        ((device_ != SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK && removed == device_) || removed == LogicalDevice());
}

sdl_audio::RemovalResult sdl_audio::Playback::HandleDeviceRemoval(SDL_AudioDeviceID removed, bool recording,
    std::uint16_t speed, int volume)
{
    if (!MatchesRemoval(removed, recording)) return RemovalResult::ignored;
    const bool wasActive = active_;
    if (Open(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, speed, volume, wasActive)) return RemovalResult::switchedToDefault;
    Close();
    return RemovalResult::unavailable;
}

bool sdl_audio::Playback::SetActive(bool active)
{
    if (!output_) return false;
    if (active_ == active) return true;
    if (!DrainSubmission() || !SDL_PauseAudioStreamDevice(stream_.get()) || !output_->Reset()) return false;
    active_ = active;
    primingFrames_ = active ? 3 : 0;
    return true;
}

bool sdl_audio::Playback::Configure(std::uint16_t speed, int volume)
{ return output_ && DrainSubmission() && output_->Configure(speed, volume); }
bool sdl_audio::Playback::Reset()
{
    if (!output_ || !DrainSubmission() || (active_ && !SDL_PauseAudioStreamDevice(stream_.get())) || !output_->Reset()) return false;
    primingFrames_ = active_ ? 3 : 0;
    return true;
}
bool sdl_audio::Playback::SubmitFrame()
{
    if (!output_) return false;
    const auto result = queue_->Submit(*stream_, output_->TakeFrame(), active_ && primingFrames_ == 1);
    if (result == FrameQueue::Result::queued && primingFrames_ > 0) --primingFrames_;
    return result != FrameQueue::Result::failed;
}

bool sdl_audio::Playback::CheckSubmission()
{ return !queue_ || queue_->Check(); }

bool sdl_audio::Playback::DrainSubmission()
{ return !queue_ || queue_->Drain(); }

bool sdl_audio::AsyncDeviceOpen::Start(SDL_AudioDeviceID device, std::uint16_t speed, int volume)
{
    if (Pending()) return SDL_SetError("An audio device is already opening");
    if (!device) return SDL_SetError("Invalid audio device");
    try
    {
        result_.Start([device, speed, volume] {
            thread_local const SdlThreadCleanup cleanup;
            auto prepared = Playback::Prepare(device, speed, volume);
            std::string error;
            if (!prepared) error = SDL_GetError();
            return Result{device, std::move(prepared), std::move(error)};
        });
        removed_.clear();
        return true;
    }
    catch (const std::exception& error) { return SDL_SetError("Could not start opening audio output: %s", error.what()); }
}

void sdl_audio::AsyncDeviceOpen::DeviceRemoved(SDL_AudioDeviceID device)
{
    if (device && Pending() && std::ranges::find(removed_, device) == removed_.end())
        removed_.push_back(device);
}

std::optional<sdl_audio::AsyncDeviceOpen::Result> sdl_audio::AsyncDeviceOpen::Poll()
{
    return Ready() ? Finish() : std::nullopt;
}

std::optional<sdl_audio::AsyncDeviceOpen::Result> sdl_audio::AsyncDeviceOpen::Finish()
{
    if (!Pending()) return std::nullopt;
    auto opened = result_.Take();
    if (opened.prepared && (std::ranges::find(removed_, opened.device) != removed_.end() ||
        std::ranges::find(removed_, opened.prepared->LogicalDevice()) != removed_.end()))
    {
        opened.prepared.reset();
        opened.error = "Audio device disconnected while opening";
    }
    removed_.clear();
    return opened;
}
