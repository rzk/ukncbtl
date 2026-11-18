/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once
#include <filesystem>
#include <future>
#include <optional>

namespace disk_image
{
// UI-owned; the destination remains reserved until the result is collected.
class AsyncCreate
{
public:
    [[nodiscard]] bool Start(std::filesystem::path path, int tracks);
    [[nodiscard]] bool Pending() const { return result_.valid(); }
    [[nodiscard]] const std::filesystem::path& Path() const { return path_; }
    [[nodiscard]] std::optional<bool> Poll();
    [[nodiscard]] std::optional<bool> Finish();
private:
    std::filesystem::path path_;
    std::future<bool> result_;
};
}
