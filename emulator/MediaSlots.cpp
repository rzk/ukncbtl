/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "MediaSlots.h"
#include "emubase/Board.h"
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

std::expected<void, media_slots::Error> media_slots::Slots::Attach(Kind kind, int slot, const std::filesystem::path& path, bool readOnly)
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
    // Copy before attaching so allocation failure cannot leave an untracked mount.
    auto savedPath = path;
    const bool attached = kind == Kind::floppy ? board_.AttachFloppyImage(slot, path, readOnly) : board_.AttachHardImage(slot + 1, path, readOnly);
    if (!attached) return std::unexpected(Error::attach_failed);
    (kind == Kind::floppy ? floppyPaths_[slot] : hardPaths_[slot]) = std::move(savedPath);
    (kind == Kind::floppy ? floppyReadOnly_[slot] : hardReadOnly_[slot]) = readOnly;
    return {};
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
