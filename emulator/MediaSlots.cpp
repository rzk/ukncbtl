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
}

bool media_slots::Slots::Attached(Kind kind, int slot) const
{
    if (!Valid(kind, slot)) return false;
    return kind == Kind::floppy ? board_.IsFloppyImageAttached(slot) : board_.IsHardImageAttached(slot + 1);
}

bool media_slots::Slots::ReadOnly(Kind kind, int slot) const
{
    if (!Attached(kind, slot)) return false;
    return kind == Kind::floppy ? board_.IsFloppyReadOnly(slot) : board_.IsHardImageReadOnly(slot + 1);
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

std::expected<void, media_slots::Error> media_slots::Slots::Attach(Kind kind, int slot, const std::filesystem::path& path)
{
    if (!Valid(kind, slot)) return std::unexpected(Error::invalid_slot);
    if (path.empty()) return std::unexpected(Error::empty_path);
    if (Attached(kind, slot)) return std::unexpected(Error::occupied);
    // Copy before attaching so allocation failure cannot leave an untracked mount.
    auto savedPath = path;
    const bool attached = kind == Kind::floppy ? board_.AttachFloppyImage(slot, path) : board_.AttachHardImage(slot + 1, path);
    if (!attached) return std::unexpected(Error::attach_failed);
    (kind == Kind::floppy ? floppyPaths_[slot] : hardPaths_[slot]) = std::move(savedPath);
    return {};
}

std::expected<void, media_slots::Error> media_slots::Slots::Eject(Kind kind, int slot)
{
    if (!Valid(kind, slot)) return std::unexpected(Error::invalid_slot);
    if (kind == Kind::floppy) board_.DetachFloppyImage(slot);
    else board_.DetachHardImage(slot + 1);
    if (Attached(kind, slot)) return std::unexpected(Error::detach_failed);
    (kind == Kind::floppy ? floppyPaths_[slot] : hardPaths_[slot]).clear();
    return {};
}
