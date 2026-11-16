/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "PreviewSettings.h"
#include <gtest/gtest.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace
{
class PreviewSettingsTest : public testing::Test
{
protected:
    std::filesystem::path directory, path;
    void SetUp() override
    {
        directory = std::filesystem::temp_directory_path() /
            ("ukncbtl-preview-settings-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        ASSERT_TRUE(std::filesystem::create_directory(directory));
        path = directory / std::filesystem::path(u8"\u65e5\u672c.ini");
    }
    void TearDown() override
    {
        std::error_code error;
        std::filesystem::remove(path, error);
        std::filesystem::remove(directory, error);
    }
    void Write(const std::string& bytes)
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(output.write(bytes.data(), static_cast<std::streamsize>(bytes.size())));
    }
    std::string Read()
    {
        std::ifstream input(path, std::ios::binary);
        return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    }
};

TEST_F(PreviewSettingsTest, MissingFileUsesDefaultsWithoutCreatingIt)
{
    const auto settings = preview_settings::Load(path);
    ASSERT_TRUE(settings);
    EXPECT_EQ(settings->windowWidth, 960);
    EXPECT_EQ(settings->windowHeight, 720);
    EXPECT_EQ(settings->screenshotMode, 1);
    EXPECT_TRUE(settings->sound);
    EXPECT_EQ(settings->displayMode, 1);
    EXPECT_EQ(settings->paletteMode, 0);
    EXPECT_EQ(settings->speed, 1);
    EXPECT_FALSE(settings->soundAY);
    EXPECT_FALSE(settings->soundCovox);
    EXPECT_EQ(settings->volume, 100);
    EXPECT_FALSE(std::filesystem::exists(path));
}

TEST_F(PreviewSettingsTest, RoundTripsAndPreservesUnrelatedEntries)
{
    Write("; retain\n[Other]\nKey=value\n[SDLPreview]\nExtra=keep\n");
    const preview_settings::Settings input{1280, 800, 6, false, 5, 2, 0x7ffe, true, true, 35};
    ASSERT_TRUE(preview_settings::Save(path, input));
    const auto output = preview_settings::Load(path);
    ASSERT_TRUE(output);
    EXPECT_EQ(output->windowWidth, input.windowWidth);
    EXPECT_EQ(output->windowHeight, input.windowHeight);
    EXPECT_EQ(output->screenshotMode, input.screenshotMode);
    EXPECT_EQ(output->sound, input.sound);
    EXPECT_EQ(output->displayMode, input.displayMode);
    EXPECT_EQ(output->paletteMode, input.paletteMode);
    EXPECT_EQ(output->speed, input.speed);
    EXPECT_EQ(output->soundAY, input.soundAY);
    EXPECT_EQ(output->soundCovox, input.soundCovox);
    EXPECT_EQ(output->volume, input.volume);
    const auto file = IniFile::Open(path);
    ASSERT_TRUE(file);
    EXPECT_EQ(file->Document().Get(L"Other", L"Key"), L"value");
    EXPECT_EQ(file->Document().Get(L"SDLPreview", L"Extra"), L"keep");
    EXPECT_NE(Read().find("; retain"), std::string::npos);
}

TEST_F(PreviewSettingsTest, InvalidValuesFallBackIndependently)
{
    Write("[SDLPreview]\nWindowWidth=999999999\nWindowHeight=700\nScreenshotMode=999\nSound=0\nDisplayMode=8\nPaletteMode=99\nSpeed=4\nSoundAY=2\nSoundCovox=bad\nVolume=101\n");
    const auto settings = preview_settings::Load(path);
    ASSERT_TRUE(settings);
    EXPECT_EQ(settings->windowWidth, 960);
    EXPECT_EQ(settings->windowHeight, 720);
    EXPECT_EQ(settings->screenshotMode, 1);
    EXPECT_FALSE(settings->sound);
    EXPECT_EQ(settings->displayMode, 1);
    EXPECT_EQ(settings->paletteMode, 0);
    EXPECT_EQ(settings->speed, 1);
    EXPECT_FALSE(settings->soundAY);
    EXPECT_FALSE(settings->soundCovox);
    EXPECT_EQ(settings->volume, 100);
    Write("[SDLPreview]\nWindowWidth=640\nWindowHeight=480\nScreenshotMode=7\nSound=garbage\n");
    const auto second = preview_settings::Load(path);
    ASSERT_TRUE(second);
    EXPECT_EQ(second->windowWidth, 640);
    EXPECT_EQ(second->windowHeight, 480);
    EXPECT_EQ(second->screenshotMode, 7);
    EXPECT_TRUE(second->sound);
}

TEST_F(PreviewSettingsTest, InvalidInputOrCorruptFileIsNeverOverwritten)
{
    Write("[SDLPreview]\nSound=1\n");
    const auto original = Read();
    EXPECT_FALSE(preview_settings::Save(path, {1, 720, 1, true}));
    EXPECT_FALSE(preview_settings::Save(path, {960, 720, -1, true}));
    EXPECT_FALSE(preview_settings::Save(path, {960, 720, 8, true}));
    EXPECT_FALSE(preview_settings::Save(path, {960, 720, 1, true, 8, 0}));
    EXPECT_FALSE(preview_settings::Save(path, {960, 720, 1, true, 1, 3}));
    EXPECT_FALSE(preview_settings::Save(path, {960, 720, 1, true, 1, 0, 4}));
    EXPECT_FALSE(preview_settings::Save(path, {960, 720, 1, true, 1, 0, 1, false, false, 101}));
    EXPECT_EQ(Read(), original);
    Write(std::string(1, static_cast<char>(0xff)));
    const auto bad = Read();
    EXPECT_FALSE(preview_settings::Load(path));
    EXPECT_FALSE(preview_settings::Save(path, {}));
    EXPECT_EQ(Read(), bad);
    EXPECT_FALSE(preview_settings::Save(directory / "missing" / "settings.ini", {}));
}
}
