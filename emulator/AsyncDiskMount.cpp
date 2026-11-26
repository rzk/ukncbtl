/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "AsyncDiskMount.h"
#include "emubase/Emubase.h"
#include <utility>
#include <variant>

struct media_slots::PreparedDisk::Image
{
    std::filesystem::path path;
    bool readOnly;
    std::variant<PreparedFloppyImage, std::unique_ptr<CHardDrive>> image;
};

media_slots::PreparedDisk::PreparedDisk() = default;
media_slots::PreparedDisk::~PreparedDisk() = default;
media_slots::PreparedDisk::PreparedDisk(PreparedDisk&&) noexcept = default;
media_slots::PreparedDisk& media_slots::PreparedDisk::operator=(PreparedDisk&&) noexcept = default;

media_slots::PreparedDisk media_slots::PreparedDisk::Open(Kind kind, const std::filesystem::path& path, bool readOnly)
{
    PreparedDisk prepared;
    if (kind == Kind::floppy)
    {
        if (auto image = CFloppyController::OpenImage(path, readOnly))
            prepared.image_ = std::make_unique<Image>(Image{path, readOnly, std::move(*image)});
    }
    else
    {
        auto drive = std::make_unique<CHardDrive>();
        if (drive->AttachImage(path, readOnly))
            prepared.image_ = std::make_unique<Image>(Image{path, readOnly, std::move(drive)});
    }
    return prepared;
}

std::expected<void, media_slots::Error> media_slots::PreparedDisk::Install(Slots& media, int slot) &&
{
    auto image = std::move(image_);
    if (!image) return std::unexpected(Error::attach_failed);
    if (auto* floppy = std::get_if<PreparedFloppyImage>(&image->image))
        return media.InstallFloppy(slot, std::move(image->path), image->readOnly, std::move(*floppy));
    return media.InstallHardDisk(slot, std::move(image->path), image->readOnly,
        std::move(std::get<std::unique_ptr<CHardDrive>>(image->image)));
}

media_slots::AsyncDiskMount::AsyncDiskMount(background::Notify notify) : drive_(std::move(notify)) {}
media_slots::AsyncDiskMount::~AsyncDiskMount() = default;

bool media_slots::AsyncDiskMount::Start(Kind kind, std::filesystem::path path, int slot, bool readOnly)
{
    if (Pending() || path.empty() || slot < 0 || slot >= (kind == Kind::floppy ? 4 : 2)) return false;
    try
    {
        path_ = std::move(path);
        drive_.Start([kind, source = path_, readOnly]() -> PreparedDisk {
            try { return PreparedDisk::Open(kind, source, readOnly); }
            catch (...) {}
            return {};
        });
        slot_ = slot;
        kind_ = kind;
        return true;
    }
    catch (...) { path_.clear(); return false; }
}

bool media_slots::AsyncDiskMount::Ready() const
{
    return drive_.Ready();
}

std::optional<std::expected<void, media_slots::Error>> media_slots::AsyncDiskMount::Finish(Slots& media)
{
    if (!Pending()) return std::nullopt;
    auto drive = drive_.Take();
    path_.clear();
    const int slot = std::exchange(slot_, -1);
    return std::move(drive).Install(media, slot);
}

void media_slots::AsyncDiskMount::Discard()
{
    if (Pending()) (void)drive_.Take();
    path_.clear();
    slot_ = -1;
}
