/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "PreviewMediaRestore.h"
#include "MediaSlots.h"
#include "PreviewRoms.h"

std::vector<std::string> preview_media_restore::Restore(const preview_settings::Settings& settings,
    const preview_options::Options& options, preview_roms::Roms& roms, media_slots::Slots& media,
    const std::filesystem::path& protectedSettings)
{
    std::vector<std::string> errors;
    if (!settings.restoreMedia || options.smokeTest) return errors;
    if (!options.rom && !settings.systemRom.empty() && (preview_settings::SameFile(settings.systemRom, protectedSettings) || !roms.LoadSystem(settings.systemRom)))
        errors.emplace_back("System ROM");
    for (int slot = 0; slot < 2; ++slot)
        if (!options.cartridges[slot] && !settings.cartridges[slot].empty() &&
            (preview_settings::SameFile(settings.cartridges[slot], protectedSettings) || !roms.Insert(slot, settings.cartridges[slot])))
            errors.push_back("Cartridge " + std::to_string(slot + 1));
    for (int slot = 0; slot < 4; ++slot)
        if (!options.floppies[slot] && !settings.floppies[slot].empty() &&
            (preview_settings::SameFile(settings.floppies[slot], protectedSettings) || !media.Attach(media_slots::Kind::floppy, slot, settings.floppies[slot])))
            errors.push_back("Floppy " + std::to_string(slot + 1));
    for (int slot = 0; slot < 2; ++slot)
        if (!options.hardDisks[slot] && !settings.hardDisks[slot].empty() &&
            (preview_settings::SameFile(settings.hardDisks[slot], protectedSettings) || !media.Attach(media_slots::Kind::hardDisk, slot, settings.hardDisks[slot])))
            errors.push_back("Hard disk " + std::to_string(slot + 1));
    return errors;
}
