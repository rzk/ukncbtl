/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once
#include <cstdint>
#include <filesystem>
#include <future>
#include <optional>
#include <vector>

namespace preview_snapshot
{
// Owned by the UI thread. The captured bytes belong to the worker; the path
// remains reserved until the result is collected. Destruction waits for it.
class AsyncSave
{
public:
    [[nodiscard]] bool Start(std::filesystem::path path, std::vector<std::uint8_t> image);
    [[nodiscard]] bool Pending() const { return result_.valid(); }
    [[nodiscard]] const std::filesystem::path& Path() const { return path_; }
    [[nodiscard]] std::optional<bool> Poll();
    [[nodiscard]] std::optional<bool> Finish();
private:
    std::filesystem::path path_;
    std::future<bool> result_;
};

class AsyncLoad
{
public:
    using Image = std::vector<std::uint8_t>;
    struct Result { bool cancelled; std::optional<Image> image; };
    [[nodiscard]] bool Start(std::filesystem::path path);
    [[nodiscard]] bool Pending() const { return result_.valid(); }
    [[nodiscard]] bool Cancelled() const { return Pending() && cancelled_; }
    // Cancellation discards the result; the reader and reservation remain
    // alive until the read completes and is collected.
    void Cancel() { if (Pending()) cancelled_ = true; }
    [[nodiscard]] const std::filesystem::path& Path() const { return path_; }
    [[nodiscard]] std::optional<Result> Poll();
    [[nodiscard]] std::optional<Result> Finish();
private:
    std::filesystem::path path_;
    std::future<std::optional<Image>> result_;
    bool cancelled_ = false;
};
}
