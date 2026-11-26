/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once
#include "BackgroundJob.h"
#include "ScreenshotClipboard.h"
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace screenshot_clipboard
{
// Owner methods run on the SDL video thread. Workers prepare owned inputs.
// Collect the latest pending request first. Failed writes leave earlier
// requests eligible to publish, and successful writes supersede older work.
class AsyncCopy
{
public:
    enum class Kind { image, text };
    enum class Outcome { copied, superseded, failed };
    struct Completion
    {
        Kind kind;
        Outcome outcome;
        bool operator==(const Completion&) const = default;
    };
    explicit AsyncCopy(background::Notify notify = {}) : image_(notify), text_(std::move(notify)) {}
    [[nodiscard]] bool Start(const screenshot_image::Image& image);
    [[nodiscard]] bool StartScaled(const std::vector<std::uint32_t>& pixels, int mode);
    // The callback owns its inputs and runs on a worker without SDL video access.
    [[nodiscard]] bool StartText(std::move_only_function<std::optional<std::string>()> prepare);
    [[nodiscard]] bool ImagePending() const { return image_.result.Pending(); }
    [[nodiscard]] bool TextPending() const { return text_.result.Pending(); }
    [[nodiscard]] bool Pending() const { return ImagePending() || TextPending(); }
    [[nodiscard]] bool SetText(const char* text);
    [[nodiscard]] std::optional<Completion> Poll();
    [[nodiscard]] std::optional<Completion> Finish();
private:
    template<class Result>
    struct Request
    {
        explicit Request(background::Notify notify) : result(std::move(notify)) {}
        background::Job<Result> result;
        std::uint64_t order = 0;
    };
    [[nodiscard]] std::optional<Kind> NewestPending() const;
    [[nodiscard]] Completion Finish(Kind kind);
    Request<std::expected<Prepared, Error>> image_;
    Request<std::optional<std::string>> text_;
    std::uint64_t nextOrder_ = 0, lastWritten_ = 0;
};
}
