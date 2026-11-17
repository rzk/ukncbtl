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
    EXPECT_FALSE(settings->mouse);
    EXPECT_FALSE(settings->keyboard);
    EXPECT_TRUE(settings->autostart);
    EXPECT_FALSE(settings->spriteViewer);
    EXPECT_EQ(settings->spriteAddress, 0);
    EXPECT_EQ(settings->spriteWidth, 2);
    EXPECT_EQ(settings->spriteMode, 0);
    EXPECT_FALSE(settings->debuggerVisible);
    EXPECT_FALSE(settings->consoleVisible);
    EXPECT_FALSE(settings->debuggerPpu);
    EXPECT_EQ(settings->networkStation, 0);
    EXPECT_EQ(settings->cpuBreakpoints, preview_settings::EmptyBreakpoints());
    EXPECT_EQ(settings->ppuBreakpoints, preview_settings::EmptyBreakpoints());
    EXPECT_FALSE(std::filesystem::exists(path));
}

TEST_F(PreviewSettingsTest, ImportsCompatibleLegacyValuesAndKeepsUnrelatedSettings)
{
    Write("[UKNCBTL]\nSound=0\nSoundAY=1\nMouse=1\nKeyboard=0\nAutostart=0\n"
          "ScreenViewMode=2\nScreenshotMode=6\nRealSpeed=2\nSoundVolume=32768\n"
          "Debug=1\nDebugCpuPpu=1\nNetStation=123\nSpriteWidth=7\nSpriteMode=3\n");
    preview_settings::Settings current;
    current.windowWidth = 1200;
    current.cpuBreakpoints[0] = 01000;
    const auto result = preview_settings::ImportLegacy(path, preview_settings::ImportEncoding::unicode, current);
    ASSERT_TRUE(result);
    EXPECT_FALSE(result->sound);
    EXPECT_TRUE(result->soundAY);
    EXPECT_TRUE(result->mouse);
    EXPECT_FALSE(result->keyboard);
    EXPECT_FALSE(result->autostart);
    EXPECT_EQ(result->paletteMode, 2);
    EXPECT_EQ(result->screenshotMode, 6);
    EXPECT_EQ(result->speed, 2);
    EXPECT_EQ(result->volume, 50);
    EXPECT_TRUE(result->debuggerVisible);
    EXPECT_FALSE(result->debuggerPpu);
    EXPECT_EQ(result->networkStation, 123);
    EXPECT_EQ(result->spriteWidth, 7);
    EXPECT_EQ(result->spriteMode, 3);
    EXPECT_EQ(result->windowWidth, 1200);
    EXPECT_EQ(result->cpuBreakpoints[0], 01000);
    EXPECT_NE(Read().find("[UKNCBTL]"), std::string::npos);
}

TEST_F(PreviewSettingsTest, MediaPathsRoundTripAndInvalidEncodedPathsAreIgnored)
{
    preview_settings::Settings input;
    input.restoreMedia = true;
    input.systemRom = directory / std::filesystem::path(u8"\u65e5\u672c rom.bin");
    input.floppies[2] = directory / std::filesystem::path(u8"\u0414\u0438\u0441\u043a;1.dsk");
    input.cartridges[0] = directory / "cart.bin";
    input.hardDisks[1] = directory / "disk.img";
    ASSERT_TRUE(preview_settings::Save(path, input));
    const auto loaded = preview_settings::Load(path);
    ASSERT_TRUE(loaded);
    EXPECT_TRUE(loaded->restoreMedia);
    EXPECT_EQ(loaded->systemRom, input.systemRom);
    EXPECT_EQ(loaded->floppies, input.floppies);
    EXPECT_EQ(loaded->cartridges, input.cartridges);
    EXPECT_EQ(loaded->hardDisks, input.hardDisks);
    Write("[SDLPreview]\nRestoreMedia=1\nFloppyPath0=0061\nFloppyPath1=not-hex\nFloppyPath2=ff\n");
    const auto malformed = preview_settings::Load(path);
    ASSERT_TRUE(malformed);
    for (const auto& value : malformed->floppies) EXPECT_TRUE(value.empty());
}

TEST_F(PreviewSettingsTest, SameFileDetectsExistingAndFuturePathAliases)
{
    const auto alias = directory / "alias.ini";
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove(path, ec); } } cleanup{alias};
    EXPECT_TRUE(preview_settings::SameFile(path, directory / "." / path.filename()));
    EXPECT_FALSE(preview_settings::SameFile(path, alias));
    EXPECT_FALSE(preview_settings::SameFile({}, path));
    Write("[SDLPreview]\nSound=1\n");
    std::error_code error;
    std::filesystem::create_hard_link(path, alias, error);
    if (!error) EXPECT_TRUE(preview_settings::SameFile(path, alias));
}

TEST_F(PreviewSettingsTest, ImportRejectsUnrecognizedFilesAndIgnoresInvalidValues)
{
    Write("[Other]\nSound=1\n");
    EXPECT_FALSE(preview_settings::ImportLegacy(path, preview_settings::ImportEncoding::unicode, {}));
    Write("[UKNCBTL]\nSound=2\nRealSpeed=65536\nSpriteWidth=0\nMouse=1\n");
    preview_settings::Settings current;
    current.sound = false;
    current.speed = 2;
    current.spriteWidth = 8;
    const auto result = preview_settings::ImportLegacy(path, preview_settings::ImportEncoding::windows1251, current);
    ASSERT_TRUE(result);
    EXPECT_FALSE(result->sound);
    EXPECT_EQ(result->speed, 2);
    EXPECT_EQ(result->spriteWidth, 8);
    EXPECT_TRUE(result->mouse);
    Write("\xff\xfe[\0");
    EXPECT_FALSE(preview_settings::ImportLegacy(path, preview_settings::ImportEncoding::windows1252, current));
}

TEST_F(PreviewSettingsTest, RoundTripsAndPreservesUnrelatedEntries)
{
    Write("; retain\n[Other]\nKey=value\n[SDLPreview]\nExtra=keep\n");
    preview_settings::Settings input{1280, 800, 6, false, 5, 2, 0x7ffe, true, true, 35, true, true, false,
        true, 012345, 8, 3};
    input.debuggerVisible = true; input.consoleVisible = true; input.debuggerPpu = true;
    input.networkStation = 01234;
    input.memorySource = 5; input.memoryBytes = true; input.memoryHex = true;
    input.toolbar = false; input.statusBar = false;
    input.traceMask = 0500;
    input.cpuBreakpoints[0] = 0; input.cpuBreakpoints[1] = 0177776;
    input.ppuBreakpoints[0] = 01000;
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
    EXPECT_EQ(output->mouse, input.mouse);
    EXPECT_EQ(output->keyboard, input.keyboard);
    EXPECT_EQ(output->autostart, input.autostart);
    EXPECT_EQ(output->spriteViewer, input.spriteViewer);
    EXPECT_EQ(output->spriteAddress, input.spriteAddress);
    EXPECT_EQ(output->spriteWidth, input.spriteWidth);
    EXPECT_EQ(output->spriteMode, input.spriteMode);
    EXPECT_EQ(output->debuggerVisible, input.debuggerVisible);
    EXPECT_EQ(output->consoleVisible, input.consoleVisible);
    EXPECT_EQ(output->debuggerPpu, input.debuggerPpu);
    EXPECT_EQ(output->networkStation, input.networkStation);
    EXPECT_EQ(output->memorySource, input.memorySource);
    EXPECT_EQ(output->memoryBytes, input.memoryBytes);
    EXPECT_EQ(output->memoryHex, input.memoryHex);
    EXPECT_EQ(output->toolbar, input.toolbar);
    EXPECT_EQ(output->statusBar, input.statusBar);
    EXPECT_EQ(output->traceMask, input.traceMask);
    EXPECT_EQ(output->cpuBreakpoints, input.cpuBreakpoints);
    EXPECT_EQ(output->ppuBreakpoints, input.ppuBreakpoints);
    const auto file = IniFile::Open(path);
    ASSERT_TRUE(file);
    EXPECT_EQ(file->Document().Get(L"Other", L"Key"), L"value");
    EXPECT_EQ(file->Document().Get(L"SDLPreview", L"Extra"), L"keep");
    EXPECT_NE(Read().find("; retain"), std::string::npos);
}

TEST_F(PreviewSettingsTest, InvalidOrDuplicateBreakpointsAreIgnoredIndependently)
{
    Write("[SDLPreview]\nDebuggerVisible=2\nConsoleVisible=1\nDebuggerPpu=bad\nNetworkStation=65536\n"
          "CpuBreakpoint0=1024\nCpuBreakpoint1=3\nCpuBreakpoint2=0\nCpuBreakpoint3=1024\n"
          "CpuBreakpoint4=65535\nCpuBreakpoint5=65534\nPpuBreakpoint0=2048\nPpuBreakpoint1=invalid\n");
    const auto settings = preview_settings::Load(path);
    ASSERT_TRUE(settings);
    EXPECT_FALSE(settings->debuggerVisible);
    EXPECT_TRUE(settings->consoleVisible);
    EXPECT_FALSE(settings->debuggerPpu);
    EXPECT_EQ(settings->networkStation, 0);
    EXPECT_EQ(settings->cpuBreakpoints[0], 0);
    EXPECT_EQ(settings->cpuBreakpoints[1], 1024);
    EXPECT_EQ(settings->cpuBreakpoints[2], 65534);
    EXPECT_EQ(settings->cpuBreakpoints[3], BreakpointSet::NoBreakpoint);
    EXPECT_EQ(settings->ppuBreakpoints[0], 2048);
    EXPECT_EQ(settings->ppuBreakpoints[1], BreakpointSet::NoBreakpoint);
}

TEST_F(PreviewSettingsTest, SavesOnlyPermanentBreakpointsWhileTemporaryRunIsActive)
{
    BreakpointSet points;
    points.SetTemporary(04000);
    ASSERT_TRUE(points.Add(0));
    ASSERT_TRUE(points.Add(02000));
    auto settings = preview_settings::Settings{};
    settings.cpuBreakpoints = points.PermanentAddresses();
    ASSERT_TRUE(preview_settings::Save(path, settings));
    const auto restored = preview_settings::Load(path);
    ASSERT_TRUE(restored);
    EXPECT_EQ(restored->cpuBreakpoints[0], 0);
    EXPECT_EQ(restored->cpuBreakpoints[1], 02000);
    EXPECT_EQ(restored->cpuBreakpoints[2], BreakpointSet::NoBreakpoint);
    EXPECT_EQ(restored->cpuBreakpoints, settings.cpuBreakpoints);
}

TEST_F(PreviewSettingsTest, InvalidValuesFallBackIndependently)
{
    Write("[SDLPreview]\nWindowWidth=999999999\nWindowHeight=700\nScreenshotMode=999\nSound=0\nDisplayMode=8\nPaletteMode=99\nSpeed=4\nSoundAY=2\nSoundCovox=bad\nVolume=101\nMouse=2\nKeyboard=2\nAutostart=2\nSpriteViewer=2\nSpriteAddress=65536\nSpriteWidth=0\nSpriteMode=5\n");
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
    EXPECT_FALSE(settings->mouse);
    EXPECT_FALSE(settings->keyboard);
    EXPECT_TRUE(settings->autostart);
    EXPECT_FALSE(settings->spriteViewer);
    EXPECT_EQ(settings->spriteAddress, 0);
    EXPECT_EQ(settings->spriteWidth, 2);
    EXPECT_EQ(settings->spriteMode, 0);
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
    auto badSprite = preview_settings::Settings{};
    badSprite.spriteWidth = 0;
    EXPECT_FALSE(preview_settings::Save(path, badSprite));
    badSprite.spriteWidth = 33;
    EXPECT_FALSE(preview_settings::Save(path, badSprite));
    badSprite.spriteWidth = 2; badSprite.spriteMode = 5;
    EXPECT_FALSE(preview_settings::Save(path, badSprite));
    auto badMemory = preview_settings::Settings{};
    badMemory.memorySource = 7;
    EXPECT_FALSE(preview_settings::Save(path, badMemory));
    auto badTrace = preview_settings::Settings{};
    badTrace.traceMask = 0040;
    EXPECT_FALSE(preview_settings::Save(path, badTrace));
    auto badBreakpoints = preview_settings::Settings{};
    badBreakpoints.cpuBreakpoints[0] = 1;
    EXPECT_FALSE(preview_settings::Save(path, badBreakpoints));
    badBreakpoints.cpuBreakpoints[0] = 01000; badBreakpoints.cpuBreakpoints[1] = 01000;
    EXPECT_FALSE(preview_settings::Save(path, badBreakpoints));
    EXPECT_EQ(Read(), original);
    Write(std::string(1, static_cast<char>(0xff)));
    const auto bad = Read();
    EXPECT_FALSE(preview_settings::Load(path));
    EXPECT_FALSE(preview_settings::Save(path, {}));
    EXPECT_EQ(Read(), bad);
    EXPECT_FALSE(preview_settings::Save(directory / "missing" / "settings.ini", {}));
}
}
