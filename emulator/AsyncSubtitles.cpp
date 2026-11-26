/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "AsyncSubtitles.h"
#include <utility>

bool subtitles::AsyncLoad::Start(std::filesystem::path path)
{
    if (Pending() || path.empty()) return false;
    try
    {
        path_ = std::move(path);
        result_.Start([path = path_] {
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
    if (!result_.Ready())
        return std::nullopt;
    return Finish();
}

std::optional<subtitles::AsyncLoad::Result> subtitles::AsyncLoad::Finish()
{
    if (!Pending()) return std::nullopt;
    auto items = result_.Take();
    auto path = std::move(path_);
    path_.clear();
    const bool cancelled = std::exchange(cancelled_, false);
    return Result{std::move(path), cancelled, std::move(items)};
}
