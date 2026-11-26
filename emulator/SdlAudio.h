/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once

#include "BackgroundJob.h"
#include "StereoPcmBatch.h"
#include <SDL3/SDL_audio.h>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class CMotherboard;

namespace sdl_audio
{
inline constexpr int SampleRate = 22050;
inline constexpr int Channels = 2;
inline constexpr int SamplesPerFrame = SampleRate / 50;
inline constexpr int FrameBytes = SamplesPerFrame * Channels * 2;
inline constexpr int MaxQueuedBytes = FrameBytes * 10;
using PcmFrame = sound::StereoPcmBatch<SamplesPerFrame>;

// One outstanding submission, with the PCM owned by the queue. The owner
// must drain it before changing or destroying the borrowed SDL stream.
class FrameQueue
{
public:
    enum class Result { queued, dropped, failed };
    FrameQueue();
    ~FrameQueue();
    [[nodiscard]] Result Submit(SDL_AudioStream& stream, PcmFrame frame, bool resume = false);
    [[nodiscard]] bool Check();
    [[nodiscard]] bool Drain();
private:
    struct Worker;
    std::unique_ptr<Worker> worker_;
};

// The board and stream are borrowed and must outlive this adapter. It owns the
// board's sound callback until destruction. Board frames may run on a worker,
// but calls on this adapter and destruction must be serialized with each frame.
class Output
{
public:
    Output(CMotherboard& board, SDL_AudioStream& stream);
    ~Output();
    Output(const Output&) = delete;
    Output& operator=(const Output&) = delete;

    // Submit collected samples after a board frame; old queued audio is dropped
    // if it exceeds the latency bound. Reset discards pending and queued audio.
    [[nodiscard]] bool SubmitFrame();
    [[nodiscard]] PcmFrame TakeFrame();
    [[nodiscard]] bool Reset();
    // Speed follows the legacy settings values; volume is 0..100 percent.
    [[nodiscard]] bool Configure(std::uint16_t speed, int volume);
private:
    static void Feed(void* context, unsigned short left, unsigned short right);
    CMotherboard& board_;
    SDL_AudioStream& stream_;
    PcmFrame batch_;
};

struct DeviceInfo { SDL_AudioDeviceID id; std::string name; };
[[nodiscard]] std::vector<DeviceInfo> PlaybackDevices();
enum class RemovalResult { ignored, switchedToDefault, unavailable };

// A paused SDL stream and its submission worker. No board callback is
// attached until Playback installs the device at a frame boundary.
class PreparedDevice
{
public:
    PreparedDevice() = default;
    PreparedDevice(const PreparedDevice&) = delete;
    PreparedDevice& operator=(const PreparedDevice&) = delete;
    [[nodiscard]] SDL_AudioDeviceID LogicalDevice() const;
private:
    friend class Playback;
    std::unique_ptr<SDL_AudioStream, decltype(&SDL_DestroyAudioStream)> stream_{nullptr, SDL_DestroyAudioStream};
    std::unique_ptr<FrameQueue> queue_;
    SDL_AudioDeviceID device_ = 0;
    std::uint16_t speed_ = 0;
    int volume_ = 0;
};

// Owns the stream and callback. Opening a new device leaves the old one intact
// if the replacement cannot be initialized. SDL audio must be initialized first.
// The board owner submits completed frames and serializes device controls with
// those frames. The queue's worker owns a submitted batch until it returns.
class Playback
{
public:
    explicit Playback(CMotherboard& board) : board_(board) {}
    [[nodiscard]] static std::unique_ptr<PreparedDevice> Prepare(SDL_AudioDeviceID device, std::uint16_t speed, int volume);
    [[nodiscard]] bool Install(std::unique_ptr<PreparedDevice> prepared, std::uint16_t speed, int volume, bool active);
    [[nodiscard]] bool Open(SDL_AudioDeviceID device, std::uint16_t speed, int volume, bool active);
    void Close();
    [[nodiscard]] bool IsOpen() const { return output_.has_value(); }
    [[nodiscard]] bool Active() const { return active_; }
    [[nodiscard]] SDL_AudioDeviceID Device() const { return device_; }
    // SDL also reports removal of the logical device bound to this stream.
    [[nodiscard]] SDL_AudioDeviceID LogicalDevice() const;
    [[nodiscard]] bool MatchesRemoval(SDL_AudioDeviceID removed, bool recording) const;
    [[nodiscard]] RemovalResult HandleDeviceRemoval(SDL_AudioDeviceID removed, bool recording,
        std::uint16_t speed, int volume);
    [[nodiscard]] bool SetActive(bool active);
    [[nodiscard]] bool Configure(std::uint16_t speed, int volume);
    [[nodiscard]] bool Reset();
    [[nodiscard]] bool SubmitFrame();
    [[nodiscard]] bool CheckSubmission();
    [[nodiscard]] bool DrainSubmission();
private:
    CMotherboard& board_;
    std::unique_ptr<SDL_AudioStream, decltype(&SDL_DestroyAudioStream)> stream_{nullptr, SDL_DestroyAudioStream};
    std::optional<Output> output_;
    std::unique_ptr<FrameQueue> queue_;
    SDL_AudioDeviceID device_ = SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK;
    bool active_ = false;
    int primingFrames_ = 0;
};

// One detached open at a time. SDL audio must stay initialized until the job
// and any collected device have been destroyed.
class AsyncDeviceOpen
{
public:
    struct Result
    {
        SDL_AudioDeviceID device;
        std::unique_ptr<PreparedDevice> prepared;
        std::string error;
    };
    explicit AsyncDeviceOpen(background::Notify notify = {}) : result_(std::move(notify)) {}
    [[nodiscard]] bool Start(SDL_AudioDeviceID device, std::uint16_t speed, int volume);
    [[nodiscard]] bool Pending() const { return result_.Pending(); }
    [[nodiscard]] bool Ready() const { return result_.Ready(); }
    [[nodiscard]] bool WaitFor(std::chrono::milliseconds timeout) const { return result_.WaitFor(timeout); }
    void DeviceRemoved(SDL_AudioDeviceID device);
    [[nodiscard]] std::optional<Result> Poll();
    [[nodiscard]] std::optional<Result> Finish();
private:
    std::vector<SDL_AudioDeviceID> removed_;
    background::Job<Result> result_;
};
}
