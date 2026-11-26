/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once
#include "PreviewOptions.h"
#include "PreviewSettings.h"
#include <cstdint>
#include <expected>
#include <stop_token>
#include <string>
#include <vector>

namespace media_slots { class Slots; }
namespace preview_roms { class Roms; }
namespace preview_tape { class Session; }
namespace preview_media_restore
{
// Explicit command-line media takes precedence and is not written to settings.
// Failed saved paths remain in settings so a temporarily missing device can return.
[[nodiscard]] std::vector<std::string> Restore(const preview_settings::Settings& settings,
    const preview_options::Options& options, preview_roms::Roms& roms, media_slots::Slots& media,
    const std::filesystem::path& protectedSettings = {});

struct StartupMedia
{
    std::vector<std::string> restoreErrors;
    std::vector<std::uint8_t> stateImage;
};

// Requires exclusive access to the board through the supplied sessions.
// Detached file reads can overlap. The owner installs results in request
// order. On cancellation, in-flight readers are joined and their unused results
// discarded. The state image is validated here and applied by the caller after reset.
[[nodiscard]] std::expected<StartupMedia, std::string> PrepareStartup(const preview_settings::Settings& settings,
    const preview_options::Options& options, preview_roms::Roms& roms, media_slots::Slots& media,
    preview_tape::Session& tape, const std::filesystem::path& protectedSettings, std::stop_token stop = {});
#ifdef UKNCBTL_MEDIA_TEST_HOOKS
namespace testing
{
struct ReadObserver
{
    void* context;
    void (*notify)(void* context, const std::filesystem::path& path);
};
// Keep the observer alive until all startup preparation calls finish.
void ObserveReads(const ReadObserver* observer);
}
#endif
}
