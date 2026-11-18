/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once
namespace ui_font
{
// Install the bundled font into the current ImGui context before the first frame.
[[nodiscard]] bool Install();
}
