/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once
#include <cstdint>
#include <deque>

namespace mouse_input
{
struct Rect { float x, y, width, height; };
struct Sample { std::int16_t dx, dy; bool left, right; };
class Tracker
{
public:
    void Move(float x, float y, Rect viewport);
    void Button(bool left, bool down);
    void Reset();
    [[nodiscard]] Sample Next();
private:
    struct Buttons { bool left = false, right = false; };
    bool anchored = false;
    float lastX = 0, lastY = 0, dx = 0, dy = 0;
    Rect lastViewport{};
    Buttons physical{}, delivered{};
    std::deque<Buttons> pending;
};
}
