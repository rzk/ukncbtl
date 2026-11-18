/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once

#include "ScreenshotImage.h"
#include <expected>
#include <string>

namespace screenshot_clipboard
{
enum class Error { video_not_initialized, invalid_image, encode_failed, clipboard_failed };
struct Prepared { std::string bmp; std::string png; };

// Encoding uses only the supplied image and private SDL surfaces/streams; it
// can run on a worker. Installing clipboard data must run on the video thread.
[[nodiscard]] std::expected<Prepared, Error> Encode(const screenshot_image::Image& image);
[[nodiscard]] std::expected<void, Error> SetPrepared(Prepared images);

// Offer BMP and PNG images through SDL. Call on the SDL video thread after video is
// initialized and continue processing SDL events while clipboard data is owned.
// The image data is retained until SDL releases the clipboard or quits.
[[nodiscard]] std::expected<void, Error> SetImage(const screenshot_image::Image& image);
}
