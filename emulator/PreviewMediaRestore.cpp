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
    const auto reservedForInput = [&](const std::filesystem::path& path) {
        return (options.tape && preview_settings::SameFile(path, *options.tape)) ||
            (options.state && preview_settings::SameFile(path, *options.state));
    };
    const auto conflictsWithCommandLine = [&](const std::filesystem::path& path, bool readOnly) {
        if (reservedForInput(path)) return true;
        for (std::size_t index = 0; index < options.floppies.size(); ++index)
            if (options.floppies[index] && preview_settings::SameFile(path, *options.floppies[index]) &&
                (!readOnly || !options.floppyReadOnly[index])) return true;
        for (std::size_t index = 0; index < options.hardDisks.size(); ++index)
            if (options.hardDisks[index] && preview_settings::SameFile(path, *options.hardDisks[index]) &&
                (!readOnly || !options.hardDiskReadOnly[index])) return true;
        return false;
    };
    if (!options.rom && !options.state && !settings.systemRom.empty() && (preview_settings::SameFile(settings.systemRom, protectedSettings) ||
        reservedForInput(settings.systemRom) || !roms.LoadSystem(settings.systemRom)))
        errors.emplace_back("System ROM");
    for (int slot = 0; slot < 2; ++slot)
        if (!options.cartridges[slot] && !settings.cartridges[slot].empty() &&
            (preview_settings::SameFile(settings.cartridges[slot], protectedSettings) || reservedForInput(settings.cartridges[slot]) ||
                !roms.Insert(slot, settings.cartridges[slot])))
            errors.push_back("Cartridge " + std::to_string(slot + 1));
    for (int slot = 0; slot < 4; ++slot)
        if (!options.floppies[slot] && !settings.floppies[slot].empty() &&
            (preview_settings::SameFile(settings.floppies[slot], protectedSettings) ||
                conflictsWithCommandLine(settings.floppies[slot], settings.floppyReadOnly[slot]) ||
                !media.Attach(media_slots::Kind::floppy, slot, settings.floppies[slot], settings.floppyReadOnly[slot])))
            errors.push_back("Floppy " + std::to_string(slot + 1));
    for (int slot = 0; slot < 2; ++slot)
        if (!options.hardDisks[slot] && !settings.hardDisks[slot].empty() &&
            (preview_settings::SameFile(settings.hardDisks[slot], protectedSettings) ||
                conflictsWithCommandLine(settings.hardDisks[slot], settings.hardDiskReadOnly[slot]) ||
                !media.Attach(media_slots::Kind::hardDisk, slot, settings.hardDisks[slot], settings.hardDiskReadOnly[slot])))
            errors.push_back("Hard disk " + std::to_string(slot + 1));
    return errors;
}
