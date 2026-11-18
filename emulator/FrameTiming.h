/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once

#include <chrono>
#include <cstdint>
#include <optional>

namespace frame_timing {
using Clock = std::chrono::steady_clock;

struct Update {
    std::optional<double> speedPercent;
    bool uptimeAdvanced = false;
};

// Feed completed frames and monotonic timestamps; interrupted frames are not
// counted. Starting restarts speed measurement without changing emulated uptime.
class Tracker {
public:
    explicit Tracker(unsigned framesPerSecond);
    void Start(Clock::time_point now);
    void ResetUptime();
    void SetUptime(uint32_t seconds) { uptime_ = seconds; }
    [[nodiscard]] uint32_t Uptime() const { return uptime_; }
    [[nodiscard]] unsigned FramesWithinSecond() const { return uptimeFrames_; }
    [[nodiscard]] Update OnFrame(Clock::time_point now);
private:
    unsigned frameRate_;
    unsigned uptimeFrames_ = 0;
    uint32_t uptime_ = 0;
    uint64_t measuredFrames_ = 0;
    Clock::time_point lastMeasurement_{};
};
}
