/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once
#include "MediaSlots.h"
#include <future>
#include <optional>

namespace media_slots
{
// Prepares a detached drive on a worker. The board owner collects and
// installs it between frames. Destruction waits and drops an uncollected drive.
class AsyncDiskMount
{
public:
    AsyncDiskMount();
    ~AsyncDiskMount();
    [[nodiscard]] bool Start(Kind kind, std::filesystem::path path, int slot, bool readOnly);
    [[nodiscard]] bool Pending() const { return drive_.valid(); }
    [[nodiscard]] const std::filesystem::path& Path() const { return path_; }
    [[nodiscard]] int Slot() const { return slot_; }
    [[nodiscard]] Kind DiskKind() const { return kind_; }
    [[nodiscard]] std::optional<std::expected<void, Error>> Poll(Slots& media);
    [[nodiscard]] std::optional<std::expected<void, Error>> Finish(Slots& media);
private:
    struct PreparedDisk;
    Kind kind_ = Kind::floppy;
    std::filesystem::path path_;
    int slot_ = -1;
    bool readOnly_ = false;
    std::future<std::unique_ptr<PreparedDisk>> drive_;
};
}
