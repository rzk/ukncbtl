/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "AsyncSubtitles.h"
#include <chrono>
#include <utility>

bool subtitles::AsyncLoad::Start(std::filesystem::path path)
{
    if (Pending() || path.empty()) return false;
    try
    {
        path_ = std::move(path);
        result_ = std::async(std::launch::async, [path = path_] {
            try { return subtitles::LoadFile(path); }
            catch (...) { return std::expected<Items, FileError>(std::unexpected(FileError::read_failed)); }
        });
        cancelled_ = false;
        return true;
    }
    catch (...) { path_.clear(); return false; }
}

std::optional<subtitles::AsyncLoad::Result> subtitles::AsyncLoad::Poll()
{
    if (!Pending() || result_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return std::nullopt;
    return Finish();
}

std::optional<subtitles::AsyncLoad::Result> subtitles::AsyncLoad::Finish()
{
    if (!Pending()) return std::nullopt;
    auto items = result_.get();
    auto path = std::move(path_);
    path_.clear();
    const bool cancelled = std::exchange(cancelled_, false);
    return Result{std::move(path), cancelled, std::move(items)};
}
