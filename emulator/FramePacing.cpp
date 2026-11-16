/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "FramePacing.h"
#include <algorithm>

bool frame_pacing::IsSupportedSpeed(std::uint16_t speed)
{
    return speed <= 3 || speed == 0x7fff || speed == 0x7ffe || speed == 0x7ffd;
}

float frame_pacing::AudioRatio(std::uint16_t speed)
{
    switch (speed)
    {
    case 0: return 5.0f;
    case 2: return 2.0f;
    case 3: return 4.0f;
    case 0x7fff: return 0.5f;
    case 0x7ffe: return 0.25f;
    case 0x7ffd: return 0.1f;
    default: return 1.0f;
    }
}

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

void frame_pacing::Scheduler::FrameStarted(Clock::time_point now, std::uint16_t speed, unsigned framesPerSecond)
{
    const auto target = Target(speed, framesPerSecond);
    next_ = target ? now + std::chrono::duration_cast<Clock::duration>(*target) : now;
}

frame_pacing::Duration frame_pacing::Scheduler::Delay(Clock::time_point now, std::uint16_t speed, bool running) const
{
    constexpr Duration uiInterval = std::chrono::milliseconds(20);
    if (!running) return uiInterval;
    if (speed == 0 || now >= next_) return Duration::zero();
    return std::min(uiInterval, std::chrono::duration_cast<Duration>(next_ - now));
}
