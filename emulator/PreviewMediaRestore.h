/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once
#include "PreviewOptions.h"
#include "PreviewSettings.h"
#include <string>
#include <vector>

namespace media_slots { class Slots; }
namespace preview_roms { class Roms; }
namespace preview_media_restore
{
// Explicit command-line media takes precedence and is not written to settings.
// Failed saved paths remain in settings so a temporarily missing device can return.
[[nodiscard]] std::vector<std::string> Restore(const preview_settings::Settings& settings,
    const preview_options::Options& options, preview_roms::Roms& roms, media_slots::Slots& media,
    const std::filesystem::path& protectedSettings = {});
}
