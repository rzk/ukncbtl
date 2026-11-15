/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "util/PngFile.h"
#include <SDL3/SDL.h>
#include <gtest/gtest.h>
#include <array>
#include <chrono>
#include <climits>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

namespace
{
constexpr std::array<std::uint32_t, 4> pixels{0x00ff0000, 0x0000ff00, 0x000000ff, 0x00ffffff};

class PngFileTest : public testing::Test
{
protected:
    std::filesystem::path directory, path;
    void SetUp() override
    {
        directory = std::filesystem::temp_directory_path() /
            ("ukncbtl-png-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        ASSERT_TRUE(std::filesystem::create_directory(directory));
        path = directory / std::filesystem::path(u8"\u65e5\u672c.png");
    }
    void TearDown() override
    {
        std::error_code error;
        std::filesystem::remove(path, error);
        std::filesystem::remove(directory, error);
    }
    std::vector<char> Read() const
    {
        std::ifstream file(path, std::ios::binary);
        return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    }
};

TEST_F(PngFileTest, SavesUnicodePathWithTopDownColors)
{
    ASSERT_TRUE(png_file::Save(path, pixels, 2, 2));
    const auto encoded = Read();
    ASSERT_FALSE(encoded.empty());
    const std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> surface(
        SDL_LoadPNG_IO(SDL_IOFromConstMem(encoded.data(), encoded.size()), true), SDL_DestroySurface);
    ASSERT_TRUE(surface) << SDL_GetError();
    ASSERT_EQ(surface->w, 2);
    ASSERT_EQ(surface->h, 2);
    constexpr std::array<std::array<Uint8, 3>, 4> expected{{{0, 0, 255}, {255, 255, 255}, {255, 0, 0}, {0, 255, 0}}};
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 2; ++x)
        {
            Uint8 r = 0, g = 0, b = 0, a = 0;
            ASSERT_TRUE(SDL_ReadSurfacePixel(surface.get(), x, y, &r, &g, &b, &a));
            EXPECT_EQ((std::array{r, g, b}), expected[y * 2 + x]);
            EXPECT_EQ(a, 255);
        }
}

TEST_F(PngFileTest, RejectsInvalidInputWithoutTruncating)
{
    ASSERT_TRUE(png_file::Save(path, pixels, 2, 2));
    const auto original = Read();
    for (const auto [width, height] : {std::pair{0, 2}, std::pair{-1, 2}, std::pair{3, 2}, std::pair{INT_MAX, INT_MAX}})
    {
        const auto result = png_file::Save(path, pixels, width, height);
        ASSERT_FALSE(result);
        EXPECT_EQ(result.error(), png_file::Error::invalid_input);
        EXPECT_EQ(Read(), original);
    }
    const auto emptyPath = png_file::Save({}, pixels, 2, 2);
    ASSERT_FALSE(emptyPath);
    EXPECT_EQ(emptyPath.error(), png_file::Error::invalid_input);
    const auto missing = png_file::Save(directory / "missing" / "image.png", pixels, 2, 2);
    ASSERT_FALSE(missing);
    EXPECT_EQ(missing.error(), png_file::Error::open_failed);
}
}
