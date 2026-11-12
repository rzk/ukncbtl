/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "FrameTiming.h"
#include <gtest/gtest.h>
#include <chrono>
#include <stdexcept>

using namespace std::chrono_literals;

TEST(FrameTiming, RejectsZeroFrameRate)
{
    EXPECT_THROW(frame_timing::Tracker{0}, std::invalid_argument);
}

TEST(FrameTiming, CountsOnlyCompletedFramesAndReportsSpeed)
{
    frame_timing::Tracker timing(50);
    const frame_timing::Clock::time_point start{};
    timing.Start(start);
    for (int i = 1; i < 50; ++i)
    {
        const auto update = timing.OnFrame(start + i * 20ms);
        EXPECT_FALSE(update.uptimeAdvanced);
        EXPECT_FALSE(update.speedPercent);
    }
    const auto update = timing.OnFrame(start + 1s);
    ASSERT_TRUE(update.speedPercent);
    EXPECT_DOUBLE_EQ(*update.speedPercent, 100.0);
    EXPECT_TRUE(update.uptimeAdvanced);
    EXPECT_EQ(timing.Uptime(), 1u);
    EXPECT_EQ(timing.FramesWithinSecond(), 0u);
}

TEST(FrameTiming, RestartResetsSpeedWindowButNotUptime)
{
    frame_timing::Tracker timing(50);
    const frame_timing::Clock::time_point start{};
    timing.Start(start);
    for (int i = 1; i <= 55; ++i) (void)timing.OnFrame(start + i * 20ms);
    timing.Start(start + 30s);
    EXPECT_EQ(timing.Uptime(), 1u);
    EXPECT_EQ(timing.FramesWithinSecond(), 5u);
    const auto update = timing.OnFrame(start + 31s);
    ASSERT_TRUE(update.speedPercent);
    EXPECT_DOUBLE_EQ(*update.speedPercent, 2.0);
    EXPECT_EQ(timing.FramesWithinSecond(), 6u);
}

TEST(FrameTiming, SnapshotUptimeAndResetPreserveFrameSemantics)
{
    frame_timing::Tracker timing(50);
    const frame_timing::Clock::time_point start{};
    timing.Start(start);
    for (int i = 0; i < 49; ++i) (void)timing.OnFrame(start);
    timing.SetUptime(99);
    EXPECT_EQ(timing.FramesWithinSecond(), 49u);
    EXPECT_TRUE(timing.OnFrame(start).uptimeAdvanced);
    EXPECT_EQ(timing.Uptime(), 100u);
    timing.ResetUptime();
    EXPECT_EQ(timing.Uptime(), 0u);
    EXPECT_EQ(timing.FramesWithinSecond(), 0u);
}
