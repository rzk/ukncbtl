/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once
#include "BackgroundJob.h"
#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace preview_snapshot
{
// Owned by the UI thread. The captured bytes belong to the worker; the path
// remains reserved until the result is collected. Destruction waits for it.
class AsyncSave
{
public:
    explicit AsyncSave(background::Notify notify = {}) : result_(std::move(notify)) {}
    [[nodiscard]] bool Start(std::filesystem::path path, std::vector<std::uint8_t> image);
    [[nodiscard]] bool Pending() const { return result_.Pending(); }
    [[nodiscard]] const std::filesystem::path& Path() const { return path_; }
    [[nodiscard]] std::optional<bool> Poll();
    [[nodiscard]] std::optional<bool> Finish();
private:
    std::filesystem::path path_;
    background::Job<bool> result_;
};

class AsyncLoad
{
public:
    using Image = std::vector<std::uint8_t>;
    struct Result { bool cancelled; std::optional<Image> image; };
    explicit AsyncLoad(background::Notify notify = {}) : result_(std::move(notify)) {}
    [[nodiscard]] bool Start(std::filesystem::path path);
    [[nodiscard]] bool Pending() const { return result_.Pending(); }
    [[nodiscard]] bool Cancelled() const { return Pending() && cancelled_; }
    // Cancellation discards the result; the reader and reservation remain
    // alive until the read completes and is collected.
    void Cancel() { if (Pending()) cancelled_ = true; }
    [[nodiscard]] const std::filesystem::path& Path() const { return path_; }
    [[nodiscard]] std::optional<Result> Poll();
    [[nodiscard]] std::optional<Result> Finish();
private:
    std::filesystem::path path_;
    background::Job<std::optional<Image>> result_;
    bool cancelled_ = false;
};
}
