/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "UiAppearance.h"
#include <imgui.h>

bool ui_appearance::Apply(int theme, int scale)
{
    if (!ImGui::GetCurrentContext() || theme < 0 || theme > 2 || scale < 75 || scale > 200) return false;
    ImGuiStyle style;
    if (theme == 0) ImGui::StyleColorsDark(&style);
    else if (theme == 1) ImGui::StyleColorsLight(&style);
    else ImGui::StyleColorsClassic(&style);
    const float factor = static_cast<float>(scale) / 100.0f;
    style.ScaleAllSizes(factor);
    style.FontScaleMain = factor;
    ImGui::GetStyle() = style;
    return true;
}
