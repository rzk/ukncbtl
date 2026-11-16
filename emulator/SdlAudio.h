/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once

#include "StereoPcmBatch.h"
#include <SDL3/SDL_audio.h>
#include <cstdint>

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
}
