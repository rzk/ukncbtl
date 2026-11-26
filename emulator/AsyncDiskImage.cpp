/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "AsyncDiskImage.h"
#include "DiskImageFile.h"
#include <utility>

bool disk_image::AsyncCreate::Start(std::filesystem::path path, int tracks)
{
    if (Pending() || path.empty()) return false;
    try
    {
        path_ = std::move(path);
        result_.Start([path = path_, tracks] {
            try { return disk_image::CreatePortable(path, tracks).has_value(); }
            catch (...) { return false; }
        });
        return true;
    }
    catch (...) { path_.clear(); return false; }
}

std::optional<bool> disk_image::AsyncCreate::Poll()
{
    if (!result_.Ready())
        return std::nullopt;
    return Finish();
}

std::optional<bool> disk_image::AsyncCreate::Finish()
{
    if (!Pending()) return std::nullopt;
    const bool success = result_.Take();
    path_.clear();
    return success;
}
