/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "PreviewOptions.h"
#include "AutoBoot.h"
#include <gtest/gtest.h>
#include <array>
#include <string_view>
#include <vector>

TEST(PreviewOptions, ParsesStartupMediaAndLastBooleanOption)
{
    using namespace std::string_view_literals;
    constexpr std::array args{"--rom"sv, "rom.bin"sv, "--floppy"sv, "0"sv, "disk.dsk"sv,
        "--cartridge"sv, "2"sv, "cart.rom"sv, "--hard-disk"sv, "2"sv, "hard.img"sv,
        "--boot"sv, "7"sv, "--no-sound"sv, "--sound"sv, "--fullscreen"sv, "--windowed"sv};
    const auto options = preview_options::Parse(args);
    ASSERT_TRUE(options) << options.error();
    EXPECT_EQ(options->rom->generic_string(), "rom.bin");
    EXPECT_EQ(options->floppies[0]->generic_string(), "disk.dsk");
    EXPECT_EQ(options->cartridges[1]->generic_string(), "cart.rom");
    EXPECT_EQ(options->hardDisks[1]->generic_string(), "hard.img");
    EXPECT_EQ(options->boot, 7); EXPECT_EQ(options->sound, true); EXPECT_EQ(options->fullscreen, false);
}

TEST(PreviewOptions, PreservesUtf8PathsAndRejectsMalformedOptions)
{
    using namespace std::string_view_literals;
    const std::array pathArgs{"--floppy"sv, "3"sv, "\xe6\x97\xa5\xe6\x9c\xac.dsk"sv,
        "--settings"sv, "\xe6\x97\xa5\xe6\x9c\xac.ini"sv};
    const auto options = preview_options::Parse(pathArgs);
    ASSERT_TRUE(options);
    EXPECT_EQ(options->floppies[3]->u8string(), u8"\u65e5\u672c.dsk");
    EXPECT_EQ(options->settingsFile->u8string(), u8"\u65e5\u672c.ini");
    for (const std::vector<std::string_view> args : {
        std::vector{"--boot"sv}, {"--boot"sv,"0"sv}, {"--boot"sv,"8"sv},
        {"--boot"sv,"1"sv,"--paused"sv}, {"--floppy"sv,"4"sv,"disk"sv},
        {"--hard-disk"sv,"0"sv,"disk"sv}, {"--cartridge"sv,"2"sv},
        {"--rom"sv,"--run"sv}, {"--settings"sv}, {"--settings"sv, "--run"sv}, {"--unknown"sv}})
        EXPECT_FALSE(preview_options::Parse(args));
}

TEST(AutoBoot, EmitsLegacyKeySequenceAtCompletedFrameBoundaries)
{
    constexpr std::array<std::uint8_t, 7> digits{0030,0031,0032,0013,0034,0035,0016};
    for (int selection = 1; selection <= 7; ++selection)
    {
        auto_boot::Sequence boot;
        ASSERT_TRUE(boot.Start(selection));
        for (int frame = 1; frame <= 120; ++frame)
        {
            const auto event = boot.FrameCompleted();
            if (frame == 106 || frame == 110 || frame == 116 || frame == 120)
            {
                ASSERT_TRUE(event);
                EXPECT_EQ(event->scan, frame < 116 ? digits[selection - 1] : 0153);
                EXPECT_EQ(event->pressed, frame == 106 || frame == 116);
            }
            else EXPECT_FALSE(event);
        }
        EXPECT_FALSE(boot.Active()); EXPECT_FALSE(boot.FrameCompleted());
    }
}

TEST(AutoBoot, InvalidStartAndCancelDoNotEmitKeys)
{
    auto_boot::Sequence boot;
    EXPECT_FALSE(boot.Start(0)); EXPECT_FALSE(boot.Start(8));
    ASSERT_TRUE(boot.Start(2));
    for (int i = 0; i < 109; ++i) (void)boot.FrameCompleted();
    boot.Cancel(); EXPECT_FALSE(boot.Active()); EXPECT_FALSE(boot.FrameCompleted());
}
