/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "FramePacing.h"
#include <gtest/gtest.h>
#include <chrono>

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
