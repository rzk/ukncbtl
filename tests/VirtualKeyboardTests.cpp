/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "VirtualKeyboard.h"
#include <gtest/gtest.h>
#include <algorithm>

TEST(VirtualKeyboard, LayoutContainsValidKeysAndSeparateKeypadEnter)
{
    const auto keys = virtual_keyboard::Keys();
    EXPECT_GE(keys.size(), 75u);
    for (const auto& key : keys)
    {
        EXPECT_NE(key.scan, 0);
        EXPECT_LT(key.scan, 128);
        EXPECT_GT(key.width, 0);
        EXPECT_LT(key.row, 6u);
    }
    EXPECT_TRUE(std::ranges::any_of(keys, [](const auto& key) { return key.scan == 0153; }));
    EXPECT_TRUE(std::ranges::any_of(keys, [](const auto& key) { return key.scan == 0166; }));
}

TEST(VirtualKeyboard, PointerKeysAreHeldUntilRelease)
{
    virtual_keyboard::State state;
    const virtual_keyboard::Key a{"A", 0072, 1, 0};
    const virtual_keyboard::Key b{"B", 0076, 1, 0};
    const auto press = state.Press(a);
    ASSERT_TRUE(press); EXPECT_TRUE(press->pressed); EXPECT_TRUE(state.Down(a.scan));
    EXPECT_FALSE(state.Press(b));
    const auto release = state.ReleasePointer();
    ASSERT_TRUE(release); EXPECT_EQ(release->scan, a.scan); EXPECT_FALSE(release->pressed);
    EXPECT_FALSE(state.Down(a.scan)); EXPECT_FALSE(state.ReleasePointer());
}

TEST(VirtualKeyboard, ModifiersLatchWhileOtherKeysArePressedAndResetTogether)
{
    virtual_keyboard::State state;
    const virtual_keyboard::Key shift{"Shift", 0105, 1, 0, true};
    const virtual_keyboard::Key ctrl{"Ctrl", 0046, 1, 0, true};
    const virtual_keyboard::Key a{"A", 0072, 1, 0};
    ASSERT_TRUE(state.Press(shift)->pressed);
    ASSERT_TRUE(state.Press(ctrl)->pressed);
    ASSERT_TRUE(state.Press(a)->pressed);
    ASSERT_TRUE(state.ReleasePointer());
    EXPECT_TRUE(state.Down(shift.scan)); EXPECT_TRUE(state.Down(ctrl.scan));
    ASSERT_FALSE(state.Press(shift)->pressed);
    EXPECT_FALSE(state.Down(shift.scan));
    state.Reset();
    EXPECT_FALSE(state.Down(ctrl.scan));
}
