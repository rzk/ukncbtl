/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "FrameTiming.h"
#include <stdexcept>

namespace frame_timing {
Tracker::Tracker(unsigned framesPerSecond) : frameRate_(framesPerSecond)
{
    if (frameRate_ == 0) throw std::invalid_argument("frame rate must be positive");
}

void Tracker::Start(Clock::time_point now)
{
    measuredFrames_ = 0;
    lastMeasurement_ = now;
}

void Tracker::ResetUptime()
{
    uptime_ = 0;
    uptimeFrames_ = 0;
}

Update Tracker::OnFrame(Clock::time_point now)
{
    Update result;
    ++measuredFrames_;
    const auto elapsed = now - lastMeasurement_;
    if (elapsed >= std::chrono::seconds(1))
    {
        const auto seconds = std::chrono::duration<double>(elapsed).count();
        result.speedPercent = static_cast<double>(measuredFrames_) * 100.0 / (seconds * frameRate_);
        measuredFrames_ = 0;
        lastMeasurement_ = now;
    }
    if (++uptimeFrames_ >= frameRate_)
    {
        ++uptime_;
        uptimeFrames_ = 0;
        result.uptimeAdvanced = true;
    }
    return result;
}
}
