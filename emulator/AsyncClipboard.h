/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once
#include "ScreenshotClipboard.h"
#include <future>
#include <optional>

namespace screenshot_clipboard
{
// Owner methods run on the SDL video thread. The worker only encodes owned
// pixels. Superseding a copy keeps the worker slot occupied until collection.
class AsyncCopy
{
public:
    enum class Outcome { copied, superseded, failed };
    [[nodiscard]] bool Start(const screenshot_image::Image& image);
    [[nodiscard]] bool StartScaled(const std::vector<std::uint32_t>& pixels, int mode);
    [[nodiscard]] bool Pending() const { return result_.valid(); }
    [[nodiscard]] bool SetText(const char* text);
    [[nodiscard]] std::optional<Outcome> Poll();
    [[nodiscard]] std::optional<Outcome> Finish();
private:
    bool superseded_ = false;
    std::future<std::expected<Prepared, Error>> result_;
};
}
