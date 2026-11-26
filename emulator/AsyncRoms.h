/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once
#include "BackgroundJob.h"
#include "PreviewRoms.h"
#include <optional>
#include <variant>

namespace preview_roms
{
class AsyncLoad
{
public:
    using Image = std::variant<SystemImage, CartridgeImage>;
    struct Result {
        std::filesystem::path path;
        int slot; // -1 is the system ROM; 0 and 1 are cartridges.
        bool cancelled;
        std::expected<Image, Error> image;
    };
    explicit AsyncLoad(background::Notify notify = {}) : result_(std::move(notify)) {}
    [[nodiscard]] bool Start(std::filesystem::path path, int slot);
    [[nodiscard]] bool Pending() const { return result_.Pending(); }
    [[nodiscard]] bool Cancelled() const { return Pending() && cancelled_; }
    [[nodiscard]] int Slot() const { return slot_; }
    void Cancel() { if (Pending()) cancelled_ = true; }
    [[nodiscard]] const std::filesystem::path& Path() const { return path_; }
    [[nodiscard]] std::optional<Result> Poll();
    [[nodiscard]] std::optional<Result> Finish();
private:
    std::filesystem::path path_;
    int slot_ = -1;
    bool cancelled_ = false;
    background::Job<std::expected<Image, Error>> result_;
};
}
