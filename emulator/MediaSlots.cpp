/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "MediaSlots.h"
#include "emubase/Emubase.h"
#include <utility>

namespace
{
bool Valid(media_slots::Kind kind, int slot)
{
    return slot >= 0 && ((kind == media_slots::Kind::floppy && slot < 4) ||
                         (kind == media_slots::Kind::hardDisk && slot < 2));
}
bool SameFile(const std::filesystem::path& first, const std::filesystem::path& second)
{
    if (first == second) return true;
    std::error_code error;
    return std::filesystem::equivalent(first, second, error) && !error;
}
}

bool media_slots::Slots::Attached(Kind kind, int slot) const
{
    if (!Valid(kind, slot)) return false;
    return kind == Kind::floppy ? board_.IsFloppyImageAttached(slot) : board_.IsHardImageAttached(slot + 1);
}

bool media_slots::Slots::FlushFloppy() { return board_.FlushFloppyImage(); }

bool media_slots::Slots::ReadOnly(Kind kind, int slot) const
{
    if (!Attached(kind, slot)) return false;
    return kind == Kind::floppy ? board_.IsFloppyReadOnly(slot) : board_.IsHardImageReadOnly(slot + 1);
}

bool media_slots::Slots::RequestedReadOnly(Kind kind, int slot) const
{
    if (!Attached(kind, slot)) return false;
    return kind == Kind::floppy ? floppyReadOnly_[slot] : hardReadOnly_[slot];
}

const std::filesystem::path* media_slots::Slots::Path(Kind kind, int slot) const
{
    if (!Attached(kind, slot)) return nullptr;
    return kind == Kind::floppy ? &floppyPaths_[slot] : &hardPaths_[slot];
}

bool media_slots::Slots::Conflicts(const std::filesystem::path& path) const
{
    if (path.empty()) return false;
    for (const auto kind : {Kind::floppy, Kind::hardDisk})
        for (int slot = 0; slot < (kind == Kind::floppy ? 4 : 2); ++slot)
            if (const auto* mounted = Path(kind, slot))
            {
                if (*mounted == path) return true;
                std::error_code error;
                if (std::filesystem::equivalent(*mounted, path, error) && !error) return true;
            }
    return false;
}

std::expected<void, media_slots::Error> media_slots::Slots::CheckAttach(
    Kind kind, int slot, const std::filesystem::path& path, bool readOnly) const
{
    if (!Valid(kind, slot)) return std::unexpected(Error::invalid_slot);
    if (path.empty()) return std::unexpected(Error::empty_path);
    if (Attached(kind, slot)) return std::unexpected(Error::occupied);
    for (const auto mountedKind : {Kind::floppy, Kind::hardDisk})
        for (int mountedSlot = 0; mountedSlot < (mountedKind == Kind::floppy ? 4 : 2); ++mountedSlot)
            if (const auto* mounted = Path(mountedKind, mountedSlot); mounted && SameFile(*mounted, path) &&
                (!readOnly || !ReadOnly(mountedKind, mountedSlot))) return std::unexpected(Error::already_mounted);
    if (kind == Kind::hardDisk && !board_.IsROMCartridgeLoaded(slot + 1))
        return std::unexpected(Error::missing_cartridge);
    return {};
}

std::expected<void, media_slots::Error> media_slots::Slots::InstallHardDisk(
    int slot, std::filesystem::path path, bool readOnly, std::unique_ptr<CHardDrive> drive)
{
    const auto allowed = CheckAttach(Kind::hardDisk, slot, path, readOnly);
    if (!allowed) return allowed;
    if (!drive) return std::unexpected(Error::attach_failed);
    drive->EnableReadAhead();
    board_.InstallHardDrive(slot + 1, std::move(drive));
    hardPaths_[slot] = std::move(path);
    hardReadOnly_[slot] = readOnly;
    return {};
}

std::expected<void, media_slots::Error> media_slots::Slots::InstallFloppy(
    int slot, std::filesystem::path path, bool readOnly, PreparedFloppyImage image)
{
    const auto allowed = CheckAttach(Kind::floppy, slot, path, readOnly);
    if (!allowed) return allowed;
    image.readAhead = true;
    if (!board_.AttachPreparedFloppy(slot, std::move(image))) return std::unexpected(Error::attach_failed);
    floppyPaths_[slot] = std::move(path);
    floppyReadOnly_[slot] = readOnly;
    return {};
}

std::expected<void, media_slots::Error> media_slots::Slots::Attach(Kind kind, int slot, const std::filesystem::path& path, bool readOnly)
{
    const auto allowed = CheckAttach(kind, slot, path, readOnly);
    if (!allowed) return allowed;
    // Copy before attaching so allocation failure cannot leave an untracked mount.
    auto savedPath = path;
    if (kind == Kind::hardDisk)
    {
        auto drive = std::make_unique<CHardDrive>();
        if (!drive->AttachImage(path, readOnly)) return std::unexpected(Error::attach_failed);
        return InstallHardDisk(slot, std::move(savedPath), readOnly, std::move(drive));
    }
    auto image = CFloppyController::OpenImage(path, readOnly);
    if (!image) return std::unexpected(Error::attach_failed);
    return InstallFloppy(slot, std::move(savedPath), readOnly, std::move(*image));
}

std::expected<void, media_slots::Error> media_slots::Slots::Eject(Kind kind, int slot)
{
    if (!Valid(kind, slot)) return std::unexpected(Error::invalid_slot);
    if (kind == Kind::floppy) board_.DetachFloppyImage(slot);
    else board_.DetachHardImage(slot + 1);
    if (Attached(kind, slot)) return std::unexpected(Error::detach_failed);
    (kind == Kind::floppy ? floppyPaths_[slot] : hardPaths_[slot]).clear();
    (kind == Kind::floppy ? floppyReadOnly_[slot] : hardReadOnly_[slot]) = false;
    return {};
}
