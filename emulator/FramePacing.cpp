/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "FramePacing.h"

std::optional<frame_pacing::Duration> frame_pacing::Target(std::uint16_t speed, unsigned framesPerSecond)
{
    if (speed == 0 || framesPerSecond == 0) return std::nullopt;
    const auto duration = Duration{std::chrono::seconds(1)} / static_cast<Duration::rep>(framesPerSecond);
    switch (speed)
    {
    case 0x7ffd: return duration * 10;
    case 0x7ffe: return duration * 4;
    case 0x7fff: return duration * 2;
    case 2: return duration / 2;
    case 3: return duration / 4;
    default: return duration;
    }
}

std::optional<frame_pacing::Duration> frame_pacing::Remaining(Duration target, Duration elapsed)
{
    if (target <= Duration::zero()) return std::nullopt;
    if (elapsed < Duration::zero()) elapsed = Duration::zero();
    if (elapsed >= target) return std::nullopt;
    return target - elapsed;
}
