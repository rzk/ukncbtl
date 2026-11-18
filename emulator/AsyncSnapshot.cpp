/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "AsyncSnapshot.h"
#include "Snapshot.h"
#include <chrono>
#include <utility>

bool preview_snapshot::AsyncSave::Start(std::filesystem::path path, std::vector<std::uint8_t> image)
{
    if (Pending()) return false;
    try
    {
        path_ = std::move(path);
        result_ = std::async(std::launch::async, [path = path_, image = std::move(image)] {
            try { return snapshot::Write(path, image); }
            catch (...) { return false; }
        });
        return true;
    }
    catch (...) { path_.clear(); return false; }
}

std::optional<bool> preview_snapshot::AsyncSave::Poll()
{
    if (!Pending() || result_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return std::nullopt;
    return Finish();
}

std::optional<bool> preview_snapshot::AsyncSave::Finish()
{
    if (!Pending()) return std::nullopt;
    const bool success = result_.get();
    path_.clear();
    return success;
}

bool preview_snapshot::AsyncLoad::Start(std::filesystem::path path)
{
    if (Pending() || path.empty()) return false;
    try
    {
        path_ = std::move(path);
        result_ = std::async(std::launch::async, [path = path_] {
            try { return snapshot::Read(path); }
            catch (...) { return std::optional<Image>{}; }
        });
        cancelled_ = false;
        return true;
    }
    catch (...) { path_.clear(); return false; }
}

std::optional<preview_snapshot::AsyncLoad::Result> preview_snapshot::AsyncLoad::Poll()
{
    if (!Pending() || result_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return std::nullopt;
    return Finish();
}

std::optional<preview_snapshot::AsyncLoad::Result> preview_snapshot::AsyncLoad::Finish()
{
    if (!Pending()) return std::nullopt;
    auto image = result_.get();
    path_.clear();
    const bool cancelled = std::exchange(cancelled_, false);
    if (cancelled) image.reset();
    return Result{cancelled, std::move(image)};
}
