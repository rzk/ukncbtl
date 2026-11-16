/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "MouseInput.h"
#include <gtest/gtest.h>
#include <limits>

TEST(MouseInput, ScalesMotionAndRetainsFractionalDeltas)
{
    mouse_input::Tracker mouse;
    const mouse_input::Rect viewport{100, 50, 1280, 576};
    mouse.Move(200, 100, viewport);
    mouse.Move(201, 101, viewport);
    auto sample = mouse.Next();
    EXPECT_EQ(sample.dx, 0);
    EXPECT_EQ(sample.dy, 0);
    mouse.Move(202, 102, viewport);
    sample = mouse.Next();
    EXPECT_EQ(sample.dx, 1);
    EXPECT_EQ(sample.dy, 1);
}

TEST(MouseInput, QueuesShortButtonTransitionsAcrossFrames)
{
    mouse_input::Tracker mouse;
    mouse.Move(10, 10, {0, 0, 640, 288});
    mouse.Button(true, true);
    mouse.Button(false, true);
    mouse.Button(true, false);
    mouse.Button(false, false);
    auto sample = mouse.Next();
    EXPECT_TRUE(sample.left); EXPECT_FALSE(sample.right);
    sample = mouse.Next();
    EXPECT_TRUE(sample.left); EXPECT_TRUE(sample.right);
    sample = mouse.Next();
    EXPECT_FALSE(sample.left); EXPECT_TRUE(sample.right);
    sample = mouse.Next();
    EXPECT_FALSE(sample.left); EXPECT_FALSE(sample.right);
}

TEST(MouseInput, LeavingViewportAndResetReleaseButtonsAndDiscardMotion)
{
    mouse_input::Tracker mouse;
    const mouse_input::Rect viewport{10, 10, 640, 288};
    mouse.Move(20, 20, viewport);
    mouse.Button(true, true);
    EXPECT_TRUE(mouse.Next().left);
    mouse.Move(30, 30, viewport);
    mouse.Move(0, 0, viewport);
    auto sample = mouse.Next();
    EXPECT_FALSE(sample.left); EXPECT_EQ(sample.dx, 0);
    mouse.Move(500, 200, viewport);
    sample = mouse.Next();
    EXPECT_EQ(sample.dx, 0); EXPECT_EQ(sample.dy, 0);
    mouse.Button(false, true);
    mouse.Reset();
    EXPECT_FALSE(mouse.Next().right);
}

TEST(MouseInput, ResizingReanchorsAndMotionIsBounded)
{
    mouse_input::Tracker mouse;
    mouse.Move(1, 1, {0, 0, 640, 288});
    mouse.Move(100, 100, {0, 0, 1280, 576});
    EXPECT_EQ(mouse.Next().dx, 0);
    mouse.Move(1200, 500, {0, 0, 1280, 576});
    const auto sample = mouse.Next();
    EXPECT_EQ(sample.dx, 256);
    EXPECT_EQ(sample.dy, 200);
    mouse.Move(std::numeric_limits<float>::quiet_NaN(), 0, {0, 0, 1, 1});
    EXPECT_EQ(mouse.Next().dx, 0);
}
