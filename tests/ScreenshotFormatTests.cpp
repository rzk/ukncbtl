/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "util/ScreenshotFormat.h"
#include <gtest/gtest.h>

TEST(ScreenshotFormat, SelectsBmpIgnoringAsciiCase)
{
    for (const auto* name : {"screen.bmp", "screen.BMP", "screen.BmP", ".bmp", "dir.name/screen.bmp"})
        EXPECT_EQ(screenshot_format::FromPath(name), screenshot_format::Format::bmp) << name;
}

TEST(ScreenshotFormat, DefaultsToPngForOtherPaths)
{
    for (const auto* name : {"", "screen", "screen.png", "screen.bmp.png", "screen.bmp/other", "screen.bmpx", "bmp"})
        EXPECT_EQ(screenshot_format::FromPath(name), screenshot_format::Format::png) << name;
}

TEST(ScreenshotFormat, HandlesUnicodeFileNames)
{
    EXPECT_EQ(screenshot_format::FromPath(std::filesystem::path(u8"\u65e5\u672c.BMP")), screenshot_format::Format::bmp);
    EXPECT_EQ(screenshot_format::FromPath(std::filesystem::path(u8"\u65e5\u672c.png")), screenshot_format::Format::png);
}
