/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "AsyncDiskMount.h"
#include "emubase/Emubase.h"
#include <chrono>
#include <utility>
#include <variant>

struct media_slots::AsyncDiskMount::PreparedDisk
{
    std::variant<PreparedFloppyImage, std::unique_ptr<CHardDrive>> image;
};

media_slots::AsyncDiskMount::AsyncDiskMount() = default;
media_slots::AsyncDiskMount::~AsyncDiskMount() = default;

bool media_slots::AsyncDiskMount::Start(Kind kind, std::filesystem::path path, int slot, bool readOnly)
{
    if (Pending() || path.empty() || slot < 0 || slot >= (kind == Kind::floppy ? 4 : 2)) return false;
    try
    {
        path_ = std::move(path);
        drive_ = std::async(std::launch::async, [kind, source = path_, readOnly]() -> std::unique_ptr<PreparedDisk> {
            try
            {
                if (kind == Kind::floppy)
                {
                    auto image = CFloppyController::OpenImage(source, readOnly);
                    if (!image) return {};
                    return std::make_unique<PreparedDisk>(PreparedDisk{std::move(*image)});
                }
                auto drive = std::make_unique<CHardDrive>();
                if (drive->AttachImage(source, readOnly))
                    return std::make_unique<PreparedDisk>(PreparedDisk{std::move(drive)});
            }
            catch (...) {}
            return {};
        });
        slot_ = slot;
        kind_ = kind;
        readOnly_ = readOnly;
        return true;
    }
    catch (...) { path_.clear(); return false; }
}

std::optional<std::expected<void, media_slots::Error>> media_slots::AsyncDiskMount::Poll(Slots& media)
{
    if (!Pending() || drive_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return std::nullopt;
    return Finish(media);
}

std::optional<std::expected<void, media_slots::Error>> media_slots::AsyncDiskMount::Finish(Slots& media)
{
    if (!Pending()) return std::nullopt;
    auto drive = drive_.get();
    auto path = std::move(path_);
    path_.clear();
    const int slot = std::exchange(slot_, -1);
    if (!drive) return std::expected<void, Error>{std::unexpected(Error::attach_failed)};
    if (kind_ == Kind::floppy)
        return media.InstallFloppy(slot, std::move(path), readOnly_, std::move(std::get<PreparedFloppyImage>(drive->image)));
    return media.InstallHardDisk(slot, std::move(path), readOnly_, std::move(std::get<std::unique_ptr<CHardDrive>>(drive->image)));
}
