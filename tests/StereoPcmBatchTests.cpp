/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "StereoPcmBatch.h"
#include <gtest/gtest.h>
#include <array>
#include <cstdint>

TEST(StereoPcmBatch, InterleavesSamplesAndRetainsFullBufferUntilReset)
{
    sound::StereoPcmBatch<2> batch;
    EXPECT_TRUE(batch.Bytes().empty());
    EXPECT_FALSE(batch.Push(0x1234, 0xabcd));
    EXPECT_TRUE(batch.Push(0x8000, 0x00ff));
    const std::array<uint8_t, 8> expected{0x34, 0x12, 0xcd, 0xab, 0, 0x80, 0xff, 0};
    const auto bytes = batch.Bytes();
    ASSERT_EQ(bytes.size(), expected.size());
    for (std::size_t i = 0; i < bytes.size(); ++i)
        EXPECT_EQ(static_cast<uint8_t>(bytes[i]), expected[i]);
    EXPECT_FALSE(batch.Push(1, 2));
    for (std::size_t i = 0; i < bytes.size(); ++i)
        EXPECT_EQ(static_cast<uint8_t>(bytes[i]), expected[i]);
    batch.Reset();
    EXPECT_TRUE(batch.Bytes().empty());
    EXPECT_FALSE(batch.Push(1, 2));
    EXPECT_EQ(batch.Bytes().size(), 4u);
}

TEST(StereoPcmBatch, SingleFrameCompletesImmediately)
{
    sound::StereoPcmBatch<1> batch;
    EXPECT_TRUE(batch.Push(0xffff, 0));
    ASSERT_EQ(batch.Bytes().size(), 4u);
    EXPECT_EQ(static_cast<uint8_t>(batch.Bytes()[0]), 0xff);
    EXPECT_EQ(static_cast<uint8_t>(batch.Bytes()[1]), 0xff);
    EXPECT_EQ(batch.Bytes()[2], 0);
    EXPECT_EQ(batch.Bytes()[3], 0);
}
