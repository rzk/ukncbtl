/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "MouseInput.h"
#include <algorithm>
#include <cmath>

void mouse_input::Tracker::Move(float x, float y, Rect rect)
{
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(rect.x) || !std::isfinite(rect.y) ||
        !std::isfinite(rect.width) || !std::isfinite(rect.height) || rect.width <= 0 || rect.height <= 0 ||
        x < rect.x || y < rect.y || x >= rect.x + rect.width || y >= rect.y + rect.height)
    {
        Reset();
        return;
    }
    if (anchored && rect.x == lastViewport.x && rect.y == lastViewport.y &&
        rect.width == lastViewport.width && rect.height == lastViewport.height)
    {
        const auto scaledX = (static_cast<double>(x) - lastX) * 640.0 / rect.width;
        const auto scaledY = (static_cast<double>(y) - lastY) * 288.0 / rect.height;
        dx = static_cast<float>(std::clamp(dx + scaledX, -256.0, 256.0));
        dy = static_cast<float>(std::clamp(dy + scaledY, -256.0, 256.0));
    }
    anchored = true;
    lastX = x; lastY = y; lastViewport = rect;
}

void mouse_input::Tracker::Button(bool left, bool down)
{
    if (!anchored) return;
    bool& state = left ? physical.left : physical.right;
    if (state == down) return;
    state = down;
    pending.push_back(physical);
}

void mouse_input::Tracker::Reset()
{
    anchored = false;
    dx = dy = 0;
    physical = delivered = {};
    pending.clear();
}

mouse_input::Sample mouse_input::Tracker::Next()
{
    if (!pending.empty()) { delivered = pending.front(); pending.pop_front(); }
    const auto x = static_cast<std::int16_t>(dx);
    const auto y = static_cast<std::int16_t>(dy);
    dx -= x; dy -= y;
    return {x, y, delivered.left, delivered.right};
}
