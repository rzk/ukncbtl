/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once
#include "BackgroundJob.h"
#include "MediaSlots.h"
#include <optional>

namespace media_slots
{
// Reads a detached image without a board. The caller installs it under
// exclusive board ownership and the slot checks are repeated then.
class PreparedDisk
{
public:
    PreparedDisk();
    ~PreparedDisk();
    PreparedDisk(PreparedDisk&&) noexcept;
    PreparedDisk& operator=(PreparedDisk&&) noexcept;
    [[nodiscard]] static PreparedDisk Open(Kind kind, const std::filesystem::path& path, bool readOnly);
    [[nodiscard]] std::expected<void, Error> Install(Slots& media, int slot) &&;
private:
    struct Image;
    std::unique_ptr<Image> image_;
};

// Prepares a detached drive on a worker. The board owner collects and
// installs it between frames. Destruction waits and drops an uncollected drive.
class AsyncDiskMount
{
public:
    explicit AsyncDiskMount(background::Notify notify = {});
    ~AsyncDiskMount();
    [[nodiscard]] bool Start(Kind kind, std::filesystem::path path, int slot, bool readOnly);
    [[nodiscard]] bool Pending() const { return drive_.Pending(); }
    [[nodiscard]] bool Ready() const;
    [[nodiscard]] const std::filesystem::path& Path() const { return path_; }
    [[nodiscard]] int Slot() const { return slot_; }
    [[nodiscard]] Kind DiskKind() const { return kind_; }
    [[nodiscard]] std::optional<std::expected<void, Error>> Finish(Slots& media);
    // Join the preparation and release the file without attaching it.
    void Discard();
private:
    Kind kind_ = Kind::floppy;
    std::filesystem::path path_;
    int slot_ = -1;
    background::Job<PreparedDisk> drive_;
};
}
