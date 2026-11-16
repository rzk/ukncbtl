/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once

#include <chrono>
#include <cstdint>
#include <optional>

namespace frame_pacing
{
using Duration = std::chrono::nanoseconds;
using Clock = std::chrono::steady_clock;

[[nodiscard]] bool IsSupportedSpeed(std::uint16_t speed);
// Matches the legacy audio playback rate; unlimited uses 500% audio.
[[nodiscard]] float AudioRatio(std::uint16_t speed);

// The speed values match the existing emulator setting: 0 is unlimited,
// 1/2/3 are 100/200/400%, and 0x7fff/0x7ffe/0x7ffd are 50/25/10%.
// Other values use 100%. Zero frame rate and unlimited speed have no target.
[[nodiscard]] std::optional<Duration> Target(std::uint16_t speed, unsigned framesPerSecond);

// Returns a remaining delay, or no delay once the target has elapsed. Negative
// elapsed time is treated as zero to handle an invalid or reset timestamp.
[[nodiscard]] std::optional<Duration> Remaining(Duration target, Duration elapsed);

// Keeps UI iterations responsive while waiting for a slow emulation frame.
class Scheduler
{
public:
    explicit Scheduler(Clock::time_point now) : next_(now) {}
    void Restart(Clock::time_point now) { next_ = now; }
    [[nodiscard]] bool Due(Clock::time_point now) const { return now >= next_; }
    void FrameStarted(Clock::time_point now, std::uint16_t speed, unsigned framesPerSecond);
    [[nodiscard]] Duration Delay(Clock::time_point now, std::uint16_t speed, bool running) const;
private:
    Clock::time_point next_;
};
}
