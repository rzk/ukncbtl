/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once
#include "BackgroundJob.h"
#include "SubtitleFile.h"
#include <optional>

namespace subtitles
{
class AsyncLoad
{
public:
    using Items = std::vector<DisasmSubtitleItem>;
    struct Result {
        std::filesystem::path path;
        bool cancelled;
        std::expected<Items, FileError> items;
    };
    explicit AsyncLoad(background::Notify notify = {}) : result_(std::move(notify)) {}
    [[nodiscard]] bool Start(std::filesystem::path path);
    [[nodiscard]] bool Pending() const { return result_.Pending(); }
    [[nodiscard]] bool Cancelled() const { return Pending() && cancelled_; }
    void Cancel() { if (Pending()) cancelled_ = true; }
    [[nodiscard]] const std::filesystem::path& Path() const { return path_; }
    [[nodiscard]] std::optional<Result> Poll();
    [[nodiscard]] std::optional<Result> Finish();
private:
    std::filesystem::path path_;
    background::Job<std::expected<Items, FileError>> result_;
    bool cancelled_ = false;
};
}
