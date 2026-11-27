/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "AsyncClipboard.h"
#include "util/SdlThreadCleanup.h"
#include <SDL3/SDL_clipboard.h>
#include <chrono>
#include <utility>
#ifdef UKNCBTL_CLIPBOARD_TEST_HOOKS
#include "ClipboardTestHooks.h"
#include <atomic>
#include <thread>

namespace
{
std::atomic<bool> holdEncoding{false};
}

void screenshot_clipboard::testing::HoldEncoding(bool hold)
{
    holdEncoding.store(hold);
}
#endif

namespace
{
std::expected<screenshot_clipboard::Prepared, screenshot_clipboard::Error> EncodeImage(const screenshot_image::Image& image)
{
#ifdef UKNCBTL_CLIPBOARD_TEST_HOOKS
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (holdEncoding.load())
    {
        if (std::chrono::steady_clock::now() >= deadline)
            return std::unexpected(screenshot_clipboard::Error::encode_failed);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
#endif
    thread_local const SdlThreadCleanup cleanup;
    return screenshot_clipboard::Encode(image);
}
}

bool screenshot_clipboard::AsyncCopy::Start(const screenshot_image::Image& image)
{
    if (ImagePending()) return false;
    try
    {
        image_.result.Start([image] { return EncodeImage(image); });
        image_.order = ++nextOrder_;
        return true;
    }
    catch (...) { return false; }
}

bool screenshot_clipboard::AsyncCopy::StartScaled(const std::vector<std::uint32_t>& pixels, int mode)
{
    if (ImagePending()) return false;
    try
    {
        image_.result.Start([pixels, mode] {
            const auto image = screenshot_image::Prepare(pixels, mode);
            if (!image) return std::expected<Prepared, Error>(std::unexpected(Error::invalid_image));
            return EncodeImage(*image);
        });
        image_.order = ++nextOrder_;
        return true;
    }
    catch (...) { return false; }
}

bool screenshot_clipboard::AsyncCopy::StartText(std::move_only_function<std::optional<std::string>()> prepare)
{
    if (TextPending() || !prepare) return false;
    try
    {
        text_.result.Start(std::move(prepare));
        text_.order = ++nextOrder_;
        return true;
    }
    catch (...) { return false; }
}

std::optional<screenshot_clipboard::AsyncCopy::Kind> screenshot_clipboard::AsyncCopy::NewestPending() const
{
    if (TextPending() && (!ImagePending() || text_.order > image_.order)) return Kind::text;
    if (ImagePending()) return Kind::image;
    return std::nullopt;
}

std::optional<screenshot_clipboard::AsyncCopy::Completion> screenshot_clipboard::AsyncCopy::Poll()
{
    const auto kind = NewestPending();
    if (!kind) return std::nullopt;
    if (!(*kind == Kind::image ? image_.result.Ready() : text_.result.Ready())) return std::nullopt;
    return Finish(*kind);
}

bool screenshot_clipboard::AsyncCopy::SetText(const char* text)
{
    if (!SDL_SetClipboardText(text)) return false;
    lastWritten_ = ++nextOrder_;
    return true;
}

std::optional<screenshot_clipboard::AsyncCopy::Completion> screenshot_clipboard::AsyncCopy::Finish()
{
    if (const auto kind = NewestPending()) return Finish(*kind);
    return std::nullopt;
}

screenshot_clipboard::AsyncCopy::Completion screenshot_clipboard::AsyncCopy::Finish(Kind kind)
{
    const auto order = kind == Kind::image ? image_.order : text_.order;
    const bool superseded = order < lastWritten_;
    bool written = false;
    try
    {
        if (kind == Kind::image)
        {
            auto encoded = image_.result.Take();
            written = !superseded && encoded && SetPrepared(std::move(*encoded));
        }
        else
        {
            auto text = text_.result.Take();
            written = !superseded && text && SDL_SetClipboardText(text->c_str());
        }
    }
    catch (...) {}
    if (written) lastWritten_ = order;
    return {kind, written ? Outcome::copied : superseded ? Outcome::superseded : Outcome::failed};
}
