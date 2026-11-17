/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once
namespace ui_appearance
{
// Theme: 0 dark, 1 light, 2 classic. Scale is a percentage from 75 to 200.
// Requires a current ImGui context; invalid inputs leave the style unchanged.
[[nodiscard]] bool Apply(int theme, int scale);
}
