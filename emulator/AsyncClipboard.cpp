/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "AsyncClipboard.h"
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
    return screenshot_clipboard::Encode(image);
}
}

bool screenshot_clipboard::AsyncCopy::Start(const screenshot_image::Image& image)
{
    if (Pending()) return false;
    try
    {
        result_ = std::async(std::launch::async, [image] { return EncodeImage(image); });
        superseded_ = false;
        return true;
    }
    catch (...) { return false; }
}

bool screenshot_clipboard::AsyncCopy::StartScaled(const std::vector<std::uint32_t>& pixels, int mode)
{
    if (Pending()) return false;
    try
    {
        result_ = std::async(std::launch::async, [pixels, mode] {
            const auto image = screenshot_image::Prepare(pixels, mode);
            if (!image) return std::expected<Prepared, Error>(std::unexpected(Error::invalid_image));
            return EncodeImage(*image);
        });
        superseded_ = false;
        return true;
    }
    catch (...) { return false; }
}

std::optional<screenshot_clipboard::AsyncCopy::Outcome> screenshot_clipboard::AsyncCopy::Poll()
{
    if (!Pending() || result_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return std::nullopt;
    return Finish();
}

bool screenshot_clipboard::AsyncCopy::SetText(const char* text)
{
    if (!SDL_SetClipboardText(text)) return false;
    if (Pending()) superseded_ = true;
    return true;
}

std::optional<screenshot_clipboard::AsyncCopy::Outcome> screenshot_clipboard::AsyncCopy::Finish()
{
    if (!Pending()) return std::nullopt;
    const bool superseded = std::exchange(superseded_, false);
    try
    {
        auto encoded = result_.get();
        if (superseded) return Outcome::superseded;
        if (encoded && SetPrepared(std::move(*encoded))) return Outcome::copied;
    }
    catch (...) {}
    return superseded ? Outcome::superseded : Outcome::failed;
}
