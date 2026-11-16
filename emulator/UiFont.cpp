/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "UiFont.h"
#include <imgui.h>

namespace
{
// Droid Sans by Steve Matteson, Apache License 2.0; see third_party/NOTICE-DroidSans.
unsigned char fontData[]{
#include "embedded_ui_font.inc"
};
}

bool ui_font::Install()
{
    if (!ImGui::GetCurrentContext()) return false;
    ImFontConfig config;
    config.FontDataOwnedByAtlas = false;
    return ImGui::GetIO().Fonts->AddFontFromMemoryTTF(fontData, static_cast<int>(sizeof(fontData)), 15.0f, &config) != nullptr;
}
