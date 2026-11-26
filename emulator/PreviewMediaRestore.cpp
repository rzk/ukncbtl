/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "PreviewMediaRestore.h"
#include "util/PathIdentity.h"
#include "AsyncDiskMount.h"
#include "MediaSlots.h"
#include "PreviewRoms.h"
#include "PreviewTape.h"
#include "Snapshot.h"
#include "util/Profiler.h"
#include <algorithm>
#include <future>
#include <memory>
#include <span>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>
#ifdef UKNCBTL_MEDIA_TEST_HOOKS
#include <atomic>

namespace
{
std::atomic<const preview_media_restore::testing::ReadObserver*> readObserver{nullptr};
}

void preview_media_restore::testing::ObserveReads(const ReadObserver* observer)
{
    readObserver.store(observer);
}
#endif

namespace
{
enum class Kind { systemRom, cartridge, floppy, hardDisk, tape, state };

struct Request
{
    Kind kind;
    std::filesystem::path path;
    std::string failure;
    int slot = 0;
    bool readOnly = false;
    bool required = false;
    bool blocked = false;
};

constexpr auto StateError = "Could not load the requested state; check the file and whether it is already in use.";
constexpr auto Cancelled = "Startup media loading was canceled.";
constexpr std::size_t ConcurrentReads = 3;

media_slots::Kind DiskKind(Kind kind)
{
    return kind == Kind::floppy ? media_slots::Kind::floppy : media_slots::Kind::hardDisk;
}

void AddSavedMedia(std::vector<Request>& requests, const preview_settings::Settings& settings,
    const preview_options::Options& options, const std::filesystem::path& protectedSettings)
{
    if (!settings.restoreMedia || options.smokeTest) return;
    const auto reservedForInput = [&](const auto& path) {
        return path_identity::SameFile(path, protectedSettings) ||
            (options.tape && path_identity::SameFile(path, *options.tape)) ||
            (options.state && path_identity::SameFile(path, *options.state));
    };
    const auto conflictsWithCommandLine = [&](const auto& path, bool readOnly) {
        if (reservedForInput(path)) return true;
        for (std::size_t i = 0; i < options.floppies.size(); ++i)
            if (options.floppies[i] && path_identity::SameFile(path, *options.floppies[i]) &&
                (!readOnly || !options.floppyReadOnly[i])) return true;
        for (std::size_t i = 0; i < options.hardDisks.size(); ++i)
            if (options.hardDisks[i] && path_identity::SameFile(path, *options.hardDisks[i]) &&
                (!readOnly || !options.hardDiskReadOnly[i])) return true;
        return false;
    };
    if (!options.rom && !options.state && !settings.systemRom.empty())
        requests.push_back({Kind::systemRom, settings.systemRom, "System ROM", 0, false, false,
            reservedForInput(settings.systemRom)});
    for (int slot = 0; slot < 2; ++slot)
        if (!options.cartridges[slot] && !settings.cartridges[slot].empty())
            requests.push_back({Kind::cartridge, settings.cartridges[slot], "Cartridge " + std::to_string(slot + 1),
                slot, false, false, reservedForInput(settings.cartridges[slot])});
    const auto addDisks = [&](Kind kind, std::string_view label, const auto& saved, const auto& overridden, const auto& readOnly) {
        for (int slot = 0; slot < static_cast<int>(saved.size()); ++slot)
            if (!overridden[slot] && !saved[slot].empty())
                requests.push_back({kind, saved[slot], std::string(label) + std::to_string(slot + 1), slot, readOnly[slot],
                    false, conflictsWithCommandLine(saved[slot], readOnly[slot])});
    };
    addDisks(Kind::floppy, "Floppy ", settings.floppies, options.floppies, settings.floppyReadOnly);
    addDisks(Kind::hardDisk, "Hard disk ", settings.hardDisks, options.hardDisks, settings.hardDiskReadOnly);
}

// Preparation only accesses host files. In particular, opening a writable
// disk prepares a non-truncating handle and never writes media data.
using Prepared = std::variant<std::monostate, preview_roms::SystemImage, preview_roms::CartridgeImage,
    media_slots::PreparedDisk, preview_tape::AsyncPrepare::Result, std::vector<std::uint8_t>>;

Prepared Read(const Request& request)
{
    UKNC_PROFILE_SCOPE("Read startup media");
#ifdef UKNCBTL_MEDIA_TEST_HOOKS
    if (const auto* observer = readObserver.load()) observer->notify(observer->context, request.path);
#endif
    switch (request.kind)
    {
    case Kind::systemRom:
        if (auto image = preview_roms::ReadSystem(request.path)) return std::move(*image);
        break;
    case Kind::cartridge:
        if (auto image = preview_roms::ReadCartridge(request.path)) return std::move(*image);
        break;
    case Kind::floppy:
    case Kind::hardDisk:
        return media_slots::PreparedDisk::Open(DiskKind(request.kind), request.path, request.readOnly);
    case Kind::tape:
        return preview_tape::AsyncPrepare::Prepare(request.path, preview_tape::FileMode::playback);
    case Kind::state:
        if (auto image = snapshot::Read(request.path)) return std::move(*image);
        break;
    }
    return {};
}

bool CanInstall(const Request& request, const preview_roms::Roms& roms, const media_slots::Slots& media,
    const preview_tape::Session* tape)
{
    if (request.blocked) return false;
    switch (request.kind)
    {
    case Kind::systemRom: return true;
    case Kind::cartridge: return !roms.Inserted(request.slot);
    case Kind::floppy:
    case Kind::hardDisk:
        return media.CheckAttach(DiskKind(request.kind), request.slot, request.path, request.readOnly).has_value();
    case Kind::tape:
        return tape && !tape->Inserted() && !media.Conflicts(request.path) && !roms.Conflicts(request.path);
    case Kind::state:
        return !media.Conflicts(request.path) && !roms.Conflicts(request.path) && (!tape || !tape->Conflicts(request.path));
    }
    return false;
}

bool Install(const Request& request, Prepared&& prepared, preview_roms::Roms& roms, media_slots::Slots& media,
    preview_tape::Session* tape, preview_media_restore::StartupMedia& result)
{
    if (auto* image = std::get_if<preview_roms::SystemImage>(&prepared))
    {
        roms.InstallSystem(request.path, *image);
        return true;
    }
    if (auto* image = std::get_if<preview_roms::CartridgeImage>(&prepared))
        return roms.InstallCartridge(request.slot, request.path, *image).has_value();
    if (auto* image = std::get_if<media_slots::PreparedDisk>(&prepared))
        return std::move(*image).Install(media, request.slot).has_value();
    if (auto* stream = std::get_if<preview_tape::AsyncPrepare::Result>(&prepared))
        return tape && std::move(*stream).InsertInto(*tape);
    if (auto* state = std::get_if<std::vector<std::uint8_t>>(&prepared))
    {
        result.stateImage = std::move(*state);
        return true;
    }
    return false;
}

std::expected<preview_media_restore::StartupMedia, std::string> Load(
    std::span<const Request> requests, preview_roms::Roms& roms, media_slots::Slots& media,
    preview_tape::Session* tape, std::stop_token stop = {})
{
    preview_media_restore::StartupMedia result;
    std::vector<std::future<std::unique_ptr<Prepared>>> readers(requests.size());
    std::size_t next = requests.size() > 1 ? 0 : requests.size();
    std::size_t inFlight = 0;
    for (std::size_t i = 0; i < requests.size(); ++i)
    {
        while (!stop.stop_requested() && next < requests.size() && inFlight < ConcurrentReads)
        {
            const auto& request = requests[next];
            if (request.blocked) { ++next; continue; }
            std::error_code error;
            // An alias must wait for earlier installation and conflict checks.
            const bool alias = std::ranges::any_of(requests.first(next), [&](const auto& previous) {
                return path_identity::SameFile(previous.path, request.path);
            });
            if (alias || !std::filesystem::is_regular_file(request.path, error) ||
                media.Conflicts(request.path) || roms.Conflicts(request.path) || (tape && tape->Conflicts(request.path)))
            {
                if (next == i) ++next;
                break;
            }
            try
            {
                readers[next] = std::async(std::launch::async, [request, stop] {
                    return stop.stop_requested() ? nullptr : std::make_unique<Prepared>(Read(request));
                });
                ++inFlight;
                ++next;
            }
            catch (const std::system_error&) { next = requests.size(); } // Load the rest on the owner.
        }
        if (stop.stop_requested()) return std::unexpected(Cancelled);
        const auto& request = requests[i];
        const bool allowed = CanInstall(request, roms, media, tape);
        auto& reader = readers[i];
        const bool started = reader.valid();
        // A rejected mount discards its speculative read, including any error.
        if (started && !allowed) reader = {};
        auto prepared = reader.valid() ? reader.get() : nullptr;
        if (started) --inFlight;
        if (stop.stop_requested()) return std::unexpected(Cancelled);
        if (allowed)
        {
            auto image = prepared ? std::move(*prepared) : Read(request);
            if (stop.stop_requested()) return std::unexpected(Cancelled);
            if (Install(request, std::move(image), roms, media, tape, result)) continue;
        }
        if (request.required) return std::unexpected(request.failure);
        result.restoreErrors.push_back(request.failure);
    }
    return result;
}
}

std::vector<std::string> preview_media_restore::Restore(const preview_settings::Settings& settings,
    const preview_options::Options& options, preview_roms::Roms& roms, media_slots::Slots& media,
    const std::filesystem::path& protectedSettings)
{
    std::vector<Request> requests;
    AddSavedMedia(requests, settings, options, protectedSettings);
    return std::move(Load(requests, roms, media, nullptr).value().restoreErrors);
}

std::expected<preview_media_restore::StartupMedia, std::string> preview_media_restore::PrepareStartup(
    const preview_settings::Settings& settings, const preview_options::Options& options,
    preview_roms::Roms& roms, media_slots::Slots& media, preview_tape::Session& tape,
    const std::filesystem::path& protectedSettings, std::stop_token stop)
{
    if (stop.stop_requested()) return std::unexpected(Cancelled);
    const auto isSettings = [&](const auto& path) { return path_identity::SameFile(path, protectedSettings); };
    const auto sameAsSettings = [&](const auto& path) { return path && isSettings(*path); };
    if (sameAsSettings(options.rom) || sameAsSettings(options.tape) || sameAsSettings(options.state) ||
        std::ranges::any_of(options.floppies, sameAsSettings) ||
        std::ranges::any_of(options.cartridges, sameAsSettings) ||
        std::ranges::any_of(options.hardDisks, sameAsSettings))
        return std::unexpected("The settings file cannot be used as a media image.");

    std::vector<Request> requests;
    if (options.rom)
        requests.push_back({Kind::systemRom, *options.rom, "Could not load the requested system ROM.", 0, false, true});
    for (int slot = 0; slot < 2; ++slot)
        if (options.cartridges[slot])
            requests.push_back({Kind::cartridge, *options.cartridges[slot],
                "Could not load requested cartridge " + std::to_string(slot + 1) + ".", slot, false, true});
    for (int slot = 0; slot < 4; ++slot)
        if (options.floppies[slot])
            requests.push_back({Kind::floppy, *options.floppies[slot],
                "Could not attach requested floppy " + std::to_string(slot) + ".", slot, options.floppyReadOnly[slot], true});
    AddSavedMedia(requests, settings, options, protectedSettings);
    for (int slot = 0; slot < 2; ++slot)
        if (options.hardDisks[slot])
            requests.push_back({Kind::hardDisk, *options.hardDisks[slot],
                "Could not attach requested hard disk " + std::to_string(slot + 1) + "; check its ROM cartridge and image.",
                slot, options.hardDiskReadOnly[slot], true});

    const auto* startupTape = options.tape ? &*options.tape :
        (!options.smokeTest && settings.restoreMedia && !settings.tapePath.empty() ? &settings.tapePath : nullptr);
    if (startupTape)
        requests.push_back({Kind::tape, *startupTape,
            options.tape ? "Could not open requested tape; check its format and whether it is already in use." : "Tape",
            0, false, options.tape.has_value(),
            isSettings(*startupTape) || (options.state && path_identity::SameFile(*startupTape, *options.state))});
    if (options.state) requests.push_back({Kind::state, *options.state, StateError, 0, false, true});
    return Load(requests, roms, media, &tape, stop);
}
