/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "ScreenshotFile.h"
#include "ScreenshotClipboard.h"
#include "VideoDecoder.h"
#include <SDL3/SDL.h>
#include <gtest/gtest.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

namespace
{
TEST(ScreenshotFile, AddsOnlyMissingExtensionsAccordingToSelectedFormat)
{
    using screenshot_format::Format;
    EXPECT_EQ(screenshot_file::WithDefaultExtension("screen", Format::png), std::filesystem::path("screen.png"));
    EXPECT_EQ(screenshot_file::WithDefaultExtension("screen", Format::bmp), std::filesystem::path("screen.bmp"));
    EXPECT_EQ(screenshot_file::WithDefaultExtension("screen.BMP", Format::png), std::filesystem::path("screen.BMP"));
    EXPECT_EQ(screenshot_file::WithDefaultExtension("screen.other", Format::bmp), std::filesystem::path("screen.other"));
    EXPECT_EQ(screenshot_file::WithDefaultExtension(".bmp", Format::png), std::filesystem::path(".bmp"));
    EXPECT_EQ(screenshot_file::WithDefaultExtension(".PnG", Format::bmp), std::filesystem::path(".PnG"));
    EXPECT_TRUE(screenshot_file::WithDefaultExtension({}, Format::png).empty());
    const std::filesystem::path unicode(u8"\u65e5\u672c");
    EXPECT_EQ(screenshot_file::WithDefaultExtension(unicode, Format::png), std::filesystem::path(u8"\u65e5\u672c.png"));
}

class ScreenshotFileTest : public testing::Test
{
protected:
    const screenshot_image::Image image{2, 2, {0x00ff0000, 0x0000ff00, 0x000000ff, 0x00ffffff}};
    std::filesystem::path directory;
    void SetUp() override
    {
        directory = std::filesystem::temp_directory_path() /
            ("ukncbtl-screenshot-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        ASSERT_TRUE(std::filesystem::create_directory(directory));
    }
    void TearDown() override
    {
        std::error_code error;
        std::filesystem::remove(directory / std::filesystem::path(u8"\u65e5\u672c.BMP"), error);
        std::filesystem::remove(directory / std::filesystem::path(u8"\u65e5\u672c.dat"), error);
        std::filesystem::remove(directory, error);
    }
    std::vector<char> Read(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        EXPECT_TRUE(file);
        return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    }
};

TEST_F(ScreenshotFileTest, SavesBmpAndPngWithExpectedPixels)
{
    for (const bool bmp : {false, true})
    {
        const auto path = directory / std::filesystem::path(bmp ? u8"\u65e5\u672c.BMP" : u8"\u65e5\u672c.dat");
        ASSERT_TRUE(screenshot_file::Save(path, image));
        const auto bytes = Read(path);
        ASSERT_GT(bytes.size(), 8u);
        if (bmp) { EXPECT_EQ(bytes[0], 'B'); EXPECT_EQ(bytes[1], 'M'); }
        else { EXPECT_EQ(static_cast<unsigned char>(bytes[0]), 0x89); EXPECT_EQ(bytes[1], 'P'); }
        SDL_IOStream* io = SDL_IOFromConstMem(bytes.data(), bytes.size());
        ASSERT_NE(io, nullptr) << SDL_GetError();
        const std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> decoded(
            bmp ? SDL_LoadBMP_IO(io, true) : SDL_LoadPNG_IO(io, true), SDL_DestroySurface);
        ASSERT_TRUE(decoded) << SDL_GetError();
        EXPECT_EQ(decoded->w, 2);
        EXPECT_EQ(decoded->h, 2);
        Uint8 r = 0, g = 0, b = 0, a = 0;
        ASSERT_TRUE(SDL_ReadSurfacePixel(decoded.get(), 0, 0, &r, &g, &b, &a));
        EXPECT_EQ(r, 0); EXPECT_EQ(g, 0); EXPECT_EQ(b, 255); EXPECT_EQ(a, 255);
        ASSERT_TRUE(SDL_ReadSurfacePixel(decoded.get(), 0, 1, &r, &g, &b, &a));
        EXPECT_EQ(r, 255); EXPECT_EQ(g, 0); EXPECT_EQ(b, 0); EXPECT_EQ(a, 255);
    }
}

TEST_F(ScreenshotFileTest, ReportsEncoderErrorsWithoutOverwritingForInvalidPixels)
{
    for (const bool bmp : {false, true})
    {
        const auto path = directory / std::filesystem::path(bmp ? u8"\u65e5\u672c.BMP" : u8"\u65e5\u672c.dat");
        ASSERT_TRUE(screenshot_file::Save(path, image));
        const auto original = Read(path);
        const screenshot_image::Image invalid{2, 2, {0}};
        const auto result = screenshot_file::Save(path, invalid);
        ASSERT_FALSE(result);
        if (bmp) EXPECT_EQ(std::get<bmp_file::Error>(result.error()), bmp_file::Error::invalid_input);
        else EXPECT_EQ(std::get<png_file::Error>(result.error()), png_file::Error::invalid_input);
        EXPECT_EQ(Read(path), original);
    }
}

TEST_F(ScreenshotFileTest, FailedReplacementPreservesDestinationDirectory)
{
    const auto occupied = directory / "occupied.bmp";
    ASSERT_TRUE(std::filesystem::create_directory(occupied));
    { std::ofstream file(occupied / "keep"); file << "safe"; }
    const auto result = screenshot_file::Save(occupied, image);
    ASSERT_FALSE(result);
    ASSERT_TRUE(std::holds_alternative<staged_file::Error>(result.error()));
    EXPECT_EQ(std::get<staged_file::Error>(result.error()), staged_file::Error::replace_failed);
    EXPECT_TRUE(std::filesystem::exists(occupied / "keep"));
    std::error_code error;
    std::filesystem::remove(occupied / "keep", error);
    std::filesystem::remove(occupied, error);
}

TEST_F(ScreenshotFileTest, PreparedFrameProducesMatchingFileAndClipboardImages)
{
    ASSERT_TRUE(SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "dummy", SDL_HINT_OVERRIDE));
    ASSERT_TRUE(SDL_Init(SDL_INIT_VIDEO)) << SDL_GetError();
    struct Quit { ~Quit() { SDL_Quit(); } } quit;
    std::vector<std::uint32_t> source(video::Width * video::Height);
    for (int y = 0; y < video::Height; ++y)
        for (int x = 0; x < video::Width; ++x)
            source[y * video::Width + x] = y < video::Height / 2 ? 0x00ff0000 : 0x000000ff;
    const auto capture = screenshot_image::Prepare(source, 0);
    ASSERT_TRUE(capture);
    const auto path = directory / std::filesystem::path(u8"\u65e5\u672c.dat");
    ASSERT_TRUE(screenshot_file::Save(path, *capture));
    ASSERT_TRUE(screenshot_clipboard::SetImage(*capture)) << SDL_GetError();
    const auto fileBytes = Read(path);
    std::size_t clipboardSize = 0;
    const std::unique_ptr<void, decltype(&SDL_free)> clipboard(
        SDL_GetClipboardData("image/bmp", &clipboardSize), SDL_free);
    ASSERT_TRUE(clipboard);
    const std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> png(
        SDL_LoadPNG_IO(SDL_IOFromConstMem(fileBytes.data(), fileBytes.size()), true), SDL_DestroySurface);
    const std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> bmp(
        SDL_LoadBMP_IO(SDL_IOFromConstMem(clipboard.get(), clipboardSize), true), SDL_DestroySurface);
    ASSERT_TRUE(png); ASSERT_TRUE(bmp);
    for (auto* surface : {png.get(), bmp.get()})
    {
        EXPECT_EQ(surface->w, video::Width);
        EXPECT_EQ(surface->h, video::Height);
        Uint8 r = 0, g = 0, b = 0, a = 0;
        ASSERT_TRUE(SDL_ReadSurfacePixel(surface, 0, 0, &r, &g, &b, &a));
        EXPECT_EQ(r, 255); EXPECT_EQ(g, 0); EXPECT_EQ(b, 0);
        ASSERT_TRUE(SDL_ReadSurfacePixel(surface, 0, video::Height - 1, &r, &g, &b, &a));
        EXPECT_EQ(r, 0); EXPECT_EQ(g, 0); EXPECT_EQ(b, 255);
    }
}
}
