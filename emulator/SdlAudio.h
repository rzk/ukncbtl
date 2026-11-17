/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once

#include "StereoPcmBatch.h"
#include <SDL3/SDL_audio.h>
#include <cstdint>
#include <memory>
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

// The board and stream are borrowed and must outlive this adapter. It owns the
// board's sound callback until destruction. Call from the emulation thread.
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
    [[nodiscard]] bool Reset();
    // Speed follows the legacy settings values; volume is 0..100 percent.
    [[nodiscard]] bool Configure(std::uint16_t speed, int volume);
private:
    static void Feed(void* context, unsigned short left, unsigned short right);
    CMotherboard& board_;
    SDL_AudioStream& stream_;
    sound::StereoPcmBatch<SamplesPerFrame> batch_;
};

struct DeviceInfo { SDL_AudioDeviceID id; std::string name; };
[[nodiscard]] std::vector<DeviceInfo> PlaybackDevices();

// Owns the stream and callback. Opening a new device leaves the old one intact
// if the replacement cannot be initialized. SDL audio must be initialized first.
class Playback
{
public:
    explicit Playback(CMotherboard& board) : board_(board) {}
    [[nodiscard]] bool Open(SDL_AudioDeviceID device, std::uint16_t speed, int volume, bool active);
    void Close();
    [[nodiscard]] bool IsOpen() const { return output_ != nullptr; }
    [[nodiscard]] bool Active() const { return active_; }
    [[nodiscard]] SDL_AudioDeviceID Device() const { return device_; }
    [[nodiscard]] bool SetActive(bool active);
    [[nodiscard]] bool Configure(std::uint16_t speed, int volume);
    [[nodiscard]] bool Reset();
    [[nodiscard]] bool SubmitFrame();
private:
    CMotherboard& board_;
    std::unique_ptr<SDL_AudioStream, decltype(&SDL_DestroyAudioStream)> stream_{nullptr, SDL_DestroyAudioStream};
    std::unique_ptr<Output> output_;
    SDL_AudioDeviceID device_ = SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK;
    bool active_ = false;
};
}
