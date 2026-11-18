/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "AsyncDiskImage.h"
#include "DiskImageFile.h"
#include <chrono>
#include <utility>

bool disk_image::AsyncCreate::Start(std::filesystem::path path, int tracks)
{
    if (Pending() || path.empty()) return false;
    try
    {
        path_ = std::move(path);
        result_ = std::async(std::launch::async, [path = path_, tracks] {
            try { return disk_image::CreatePortable(path, tracks).has_value(); }
            catch (...) { return false; }
        });
        return true;
    }
    catch (...) { path_.clear(); return false; }
}

std::optional<bool> disk_image::AsyncCreate::Poll()
{
    if (!Pending() || result_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return std::nullopt;
    return Finish();
}

std::optional<bool> disk_image::AsyncCreate::Finish()
{
    if (!Pending()) return std::nullopt;
    const bool success = result_.get();
    path_.clear();
    return success;
}
