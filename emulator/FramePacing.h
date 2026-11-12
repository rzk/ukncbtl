/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once

#include <chrono>
#include <cstdint>
#include <optional>

namespace frame_pacing
{
using Duration = std::chrono::nanoseconds;

// The speed values match the existing emulator setting: 0 is unlimited,
// 1/2/3 are 100/200/400%, and 0x7fff/0x7ffe/0x7ffd are 50/25/10%.
// Other values use 100%. Zero frame rate and unlimited speed have no target.
[[nodiscard]] std::optional<Duration> Target(std::uint16_t speed, unsigned framesPerSecond);

// Returns a remaining delay, or no delay once the target has elapsed. Negative
// elapsed time is treated as zero to handle an invalid or reset timestamp.
[[nodiscard]] std::optional<Duration> Remaining(Duration target, Duration elapsed);
}
