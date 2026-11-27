/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "AsyncScreenshot.h"
#include "ScreenshotFile.h"
#include "util/SdlThreadCleanup.h"
#include <utility>

bool screenshot_file::AsyncSave::Start(std::filesystem::path path, screenshot_image::Image image)
{
    if (Pending()) return false;
    try
    {
        path_ = std::move(path);
        result_.Start([path = path_, image = std::move(image)] {
            thread_local const SdlThreadCleanup cleanup;
            try { return screenshot_file::Save(path, image).has_value(); }
            catch (...) { return false; }
        });
        return true;
    }
    catch (...) { path_.clear(); return false; }
}

bool screenshot_file::AsyncSave::StartScaled(std::filesystem::path path, std::vector<std::uint32_t> pixels, int mode)
{
    if (Pending()) return false;
    try
    {
        path_ = std::move(path);
        result_.Start([path = path_, pixels = std::move(pixels), mode] {
            thread_local const SdlThreadCleanup cleanup;
            try
            {
                const auto image = screenshot_image::Prepare(pixels, mode);
                return image && screenshot_file::Save(path, *image).has_value();
            }
            catch (...) { return false; }
        });
        return true;
    }
    catch (...) { path_.clear(); return false; }
}

std::optional<bool> screenshot_file::AsyncSave::Poll()
{
    if (!result_.Ready())
        return std::nullopt;
    return Finish();
}

std::optional<bool> screenshot_file::AsyncSave::Finish()
{
    if (!Pending()) return std::nullopt;
    const bool success = result_.Take();
    path_.clear();
    return success;
}
