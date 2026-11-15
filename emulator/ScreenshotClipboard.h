/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once

#include "ScreenshotImage.h"
#include <expected>

namespace screenshot_clipboard
{
enum class Error { video_not_initialized, invalid_image, encode_failed, clipboard_failed };

// Offer BMP and PNG images through SDL. Call on the SDL video thread after video is
// initialized and continue processing SDL events while clipboard data is owned.
// The image data is retained until SDL releases the clipboard or quits.
[[nodiscard]] std::expected<void, Error> SetImage(const screenshot_image::Image& image);
}
