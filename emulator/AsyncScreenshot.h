/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once
#include "ScreenshotImage.h"
#include <filesystem>
#include <future>
#include <optional>

namespace screenshot_file
{
// Main-thread owned. One outstanding save bounds memory and reserves its path
// until its result is collected. Destruction waits for an outstanding save.
class AsyncSave
{
public:
    [[nodiscard]] bool Start(std::filesystem::path path, screenshot_image::Image image);
    // Own the unscaled screen pixels and prepare the requested mode on the worker.
    [[nodiscard]] bool StartScaled(std::filesystem::path path, std::vector<std::uint32_t> pixels, int mode);
    [[nodiscard]] bool Pending() const { return result_.valid(); }
    [[nodiscard]] const std::filesystem::path& Path() const { return path_; }
    // No value while idle or still running; otherwise true on success.
    [[nodiscard]] std::optional<bool> Poll();
    [[nodiscard]] std::optional<bool> Finish();
private:
    std::filesystem::path path_;
    std::future<bool> result_;
};
}
