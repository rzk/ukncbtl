/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "FramePacing.h"
#include <gtest/gtest.h>
#include <chrono>
#include <utility>

using namespace std::chrono_literals;

TEST(FramePacing, ExistingSpeedsProduceExpectedFrameDurations)
{
    EXPECT_EQ(frame_pacing::Target(0, 50), std::nullopt);
    EXPECT_EQ(frame_pacing::Target(1, 50), 20ms);
    EXPECT_EQ(frame_pacing::Target(2, 50), 10ms);
    EXPECT_EQ(frame_pacing::Target(3, 50), 5ms);
    EXPECT_EQ(frame_pacing::Target(0x7fff, 50), 40ms);
    EXPECT_EQ(frame_pacing::Target(0x7ffe, 50), 80ms);
    EXPECT_EQ(frame_pacing::Target(0x7ffd, 50), 200ms);
    EXPECT_EQ(frame_pacing::Target(42, 50), 20ms);
    EXPECT_EQ(frame_pacing::Target(1, 0), std::nullopt);
}

TEST(FramePacing, SupportsSubmillisecondPrecision)
{
    EXPECT_EQ(frame_pacing::Target(1, 60), 16666666ns);
    EXPECT_EQ(frame_pacing::Target(3, 60), 4166666ns);
}

TEST(FramePacing, RemainingDelayClampsNegativeElapsedAndStopsAtDeadline)
{
    EXPECT_EQ(frame_pacing::Remaining(20ms, -1ms), 20ms);
    EXPECT_EQ(frame_pacing::Remaining(20ms, 0ms), 20ms);
    EXPECT_EQ(frame_pacing::Remaining(20ms, 5ms), 15ms);
    EXPECT_EQ(frame_pacing::Remaining(20ms, 20ms), std::nullopt);
    EXPECT_EQ(frame_pacing::Remaining(20ms, 21ms), std::nullopt);
    EXPECT_EQ(frame_pacing::Remaining(0ms, 0ms), std::nullopt);
    EXPECT_EQ(frame_pacing::Remaining(-1ms, 0ms), std::nullopt);
}

TEST(FramePacing, SupportedSpeedsAndAudioRatiosMatchLegacyBehavior)
{
    for (const auto [speed, ratio] : {std::pair{0, 5.0f}, std::pair{1, 1.0f}, std::pair{2, 2.0f},
        std::pair{3, 4.0f}, std::pair{0x7fff, 0.5f}, std::pair{0x7ffe, 0.25f}, std::pair{0x7ffd, 0.1f}})
    {
        EXPECT_TRUE(frame_pacing::IsSupportedSpeed(static_cast<std::uint16_t>(speed)));
        EXPECT_FLOAT_EQ(frame_pacing::AudioRatio(static_cast<std::uint16_t>(speed)), ratio);
    }
    EXPECT_FALSE(frame_pacing::IsSupportedSpeed(4));
    EXPECT_FLOAT_EQ(frame_pacing::AudioRatio(4), 1.0f);
}

TEST(FramePacing, SchedulerKeepsUiResponsiveAndRunsFramesAtSelectedRate)
{
    const frame_pacing::Clock::time_point start{};
    frame_pacing::Scheduler scheduler(start);
    ASSERT_TRUE(scheduler.Due(start));
    scheduler.FrameStarted(start, 0x7ffd, 50);
    EXPECT_FALSE(scheduler.Due(start + 199ms));
    EXPECT_EQ(scheduler.Delay(start, 0x7ffd, true), 20ms);
    EXPECT_EQ(scheduler.Delay(start + 195ms, 0x7ffd, true), 5ms);
    EXPECT_TRUE(scheduler.Due(start + 200ms));
    EXPECT_EQ(scheduler.Delay(start + 200ms, 0x7ffd, true), 0ms);
    scheduler.Restart(start + 300ms);
    EXPECT_TRUE(scheduler.Due(start + 300ms));
    scheduler.FrameStarted(start + 300ms, 3, 50);
    EXPECT_EQ(scheduler.Delay(start + 300ms, 3, true), 5ms);
    EXPECT_EQ(scheduler.Delay(start + 300ms, 3, false), 20ms);
    scheduler.FrameStarted(start + 400ms, 0, 50);
    EXPECT_EQ(scheduler.Delay(start + 400ms, 0, true), 0ms);
}
