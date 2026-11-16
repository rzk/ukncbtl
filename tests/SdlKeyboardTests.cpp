/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "SdlKeyboard.h"
#include "Input.h"
#include "emubase/Emubase.h"
#include <gtest/gtest.h>
#include <array>

void DebugLog(const wchar_t*) {}

TEST(SdlKeyboard, MatchesLegacyAlphabetDigitsAndKeypadInBothModes)
{
    for (bool latin : {false, true})
    {
        for (int i = 0; i < 26; ++i)
            EXPECT_EQ(sdl_keyboard::Map(static_cast<SDL_Scancode>(SDL_SCANCODE_A + i), latin), input::MapKey('A' + i, latin));
        for (int i = 0; i < 9; ++i)
        {
            EXPECT_EQ(sdl_keyboard::Map(static_cast<SDL_Scancode>(SDL_SCANCODE_1 + i), latin), input::MapKey('1' + i, latin));
            EXPECT_EQ(sdl_keyboard::Map(static_cast<SDL_Scancode>(SDL_SCANCODE_KP_1 + i), latin), input::MapKey(0x61 + i, latin));
        }
        EXPECT_EQ(sdl_keyboard::Map(SDL_SCANCODE_0, latin), input::MapKey('0', latin));
        EXPECT_EQ(sdl_keyboard::Map(SDL_SCANCODE_KP_0, latin), input::MapKey(0x60, latin));
        EXPECT_EQ(sdl_keyboard::Map(SDL_SCANCODE_RETURN, latin), 0153);
        EXPECT_EQ(sdl_keyboard::Map(SDL_SCANCODE_KP_ENTER, latin), 0166);
        EXPECT_EQ(sdl_keyboard::Map(SDL_SCANCODE_UNKNOWN, latin), 0);
        EXPECT_EQ(sdl_keyboard::Map(SDL_SCANCODE_SEMICOLON, latin), input::MapKey(0xba, latin));
        EXPECT_EQ(sdl_keyboard::Map(SDL_SCANCODE_LEFTBRACKET, latin), input::MapKey(0xdb, latin));
    }
}

TEST(SdlKeyboard, MatchesLegacyFunctionNavigationAndPunctuationMappings)
{
    constexpr std::array cases{
        std::pair{SDL_SCANCODE_BACKSPACE, 0x08}, std::pair{SDL_SCANCODE_TAB, 0x09},
        std::pair{SDL_SCANCODE_ESCAPE, 0x1b}, std::pair{SDL_SCANCODE_SPACE, 0x20},
        std::pair{SDL_SCANCODE_CAPSLOCK, 0x14}, std::pair{SDL_SCANCODE_APPLICATION, 0x5d},
        std::pair{SDL_SCANCODE_LEFT, 0x25}, std::pair{SDL_SCANCODE_UP, 0x26},
        std::pair{SDL_SCANCODE_RIGHT, 0x27}, std::pair{SDL_SCANCODE_DOWN, 0x28},
        std::pair{SDL_SCANCODE_INSERT, 0x2d}, std::pair{SDL_SCANCODE_DELETE, 0x2e},
        std::pair{SDL_SCANCODE_HOME, 0x24}, std::pair{SDL_SCANCODE_END, 0x23},
        std::pair{SDL_SCANCODE_PAGEUP, 0x21}, std::pair{SDL_SCANCODE_PAGEDOWN, 0x22},
        std::pair{SDL_SCANCODE_KP_MULTIPLY, 0x6a}, std::pair{SDL_SCANCODE_KP_MINUS, 0x6d},
        std::pair{SDL_SCANCODE_KP_PERIOD, 0x6e}, std::pair{SDL_SCANCODE_KP_DIVIDE, 0x6f},
        std::pair{SDL_SCANCODE_SEMICOLON, 0xba}, std::pair{SDL_SCANCODE_EQUALS, 0xbb},
        std::pair{SDL_SCANCODE_COMMA, 0xbc}, std::pair{SDL_SCANCODE_MINUS, 0xbd},
        std::pair{SDL_SCANCODE_PERIOD, 0xbe}, std::pair{SDL_SCANCODE_SLASH, 0xbf},
        std::pair{SDL_SCANCODE_GRAVE, 0xc0}, std::pair{SDL_SCANCODE_LEFTBRACKET, 0xdb},
        std::pair{SDL_SCANCODE_BACKSLASH, 0xdc}, std::pair{SDL_SCANCODE_RIGHTBRACKET, 0xdd},
        std::pair{SDL_SCANCODE_APOSTROPHE, 0xde}};
    for (bool latin : {false, true})
    {
        for (const auto [key, legacy] : cases)
            EXPECT_EQ(sdl_keyboard::Map(key, latin), input::MapKey(legacy, latin)) << static_cast<int>(key);
        for (int i = 0; i < 12; ++i)
            EXPECT_EQ(sdl_keyboard::Map(static_cast<SDL_Scancode>(SDL_SCANCODE_F1 + i), latin), input::MapKey(0x70 + i, latin));
    }
}

TEST(SdlKeyboard, TracksLayoutAtKeyDownAndIgnoresRepeatsAndCapture)
{
    sdl_keyboard::Keyboard keyboard;
    EXPECT_FALSE(keyboard.Transition(SDL_SCANCODE_A, true, false, true, true));
    auto event = keyboard.Transition(SDL_SCANCODE_A, true, false, false, true);
    ASSERT_TRUE(event); EXPECT_EQ(event->scan, 0072); EXPECT_TRUE(event->pressed);
    EXPECT_FALSE(keyboard.Transition(SDL_SCANCODE_A, true, true, false, false));
    EXPECT_FALSE(keyboard.Transition(SDL_SCANCODE_A, true, false, false, false));
    event = keyboard.Transition(SDL_SCANCODE_A, false, false, true, false);
    ASSERT_TRUE(event); EXPECT_EQ(event->scan, 0072); EXPECT_FALSE(event->pressed);
    EXPECT_FALSE(keyboard.Transition(SDL_SCANCODE_A, false, false, false, false));
    event = keyboard.Transition(SDL_SCANCODE_A, true, false, false, false);
    ASSERT_TRUE(event); EXPECT_EQ(event->scan, 0047); EXPECT_TRUE(event->pressed);
}

TEST(SdlKeyboard, CombinesPhysicalKeysMappedToSameEmulatedKey)
{
    sdl_keyboard::Keyboard keyboard;
    auto event = keyboard.Transition(SDL_SCANCODE_LSHIFT, true, false, false, true);
    ASSERT_TRUE(event); EXPECT_EQ(event->scan, 0105); EXPECT_TRUE(event->pressed);
    EXPECT_FALSE(keyboard.Transition(SDL_SCANCODE_RSHIFT, true, false, false, true));
    EXPECT_FALSE(keyboard.Transition(SDL_SCANCODE_LSHIFT, false, false, false, true));
    event = keyboard.Transition(SDL_SCANCODE_RSHIFT, false, false, false, true);
    ASSERT_TRUE(event); EXPECT_EQ(event->scan, 0105); EXPECT_FALSE(event->pressed);
}

TEST(SdlKeyboard, MergesVirtualAndPhysicalKeyOwnership)
{
    using sdl_keyboard::Source;
    sdl_keyboard::Merge merge;
    auto event = merge.Transition(Source::physical, {0105, true});
    ASSERT_TRUE(event); EXPECT_TRUE(event->pressed);
    EXPECT_FALSE(merge.Transition(Source::virtualKey, {0105, true}));
    EXPECT_FALSE(merge.Transition(Source::physical, {0105, false}));
    event = merge.Transition(Source::virtualKey, {0105, false});
    ASSERT_TRUE(event); EXPECT_FALSE(event->pressed);
    EXPECT_FALSE(merge.Transition(Source::virtualKey, {0105, false}));
    ASSERT_TRUE(merge.Transition(Source::virtualKey, {0072, true}));
    merge.Reset();
    EXPECT_FALSE(merge.Transition(Source::virtualKey, {0072, false}));
}

TEST(SdlKeyboard, VirtualTransitionsAreDeliveredAcrossEmulatedFrames)
{
    struct Runtime { Runtime() { CProcessor::Init(); } ~Runtime() { CProcessor::Done(); } } runtime;
    CMotherboard board;
    board.Reset();
    for (int column = 0; column < 8; ++column)
        board.KeyboardEvent(static_cast<std::uint8_t>((column << 4) | 3), false);
    board.GetCPU()->SetDCLOPin(true); board.GetPPU()->SetDCLOPin(true);
    sdl_keyboard::Merge merge;
    sdl_keyboard::Delivery delivery;
    const auto press = merge.Transition(sdl_keyboard::Source::virtualKey, {0072, true});
    const auto release = merge.Transition(sdl_keyboard::Source::virtualKey, {0072, false});
    ASSERT_TRUE(press); ASSERT_TRUE(release);
    delivery.Push(*press); delivery.Push(*release);
    auto next = delivery.Next();
    ASSERT_TRUE(next); board.KeyboardEvent(next->scan, next->pressed);
    ASSERT_TRUE(board.SystemFrame());
    EXPECT_EQ(board.GetPPUMemoryController()->GetWord(0177702, false, false), 0072);
    next = delivery.Next();
    ASSERT_TRUE(next); board.KeyboardEvent(next->scan, next->pressed);
    ASSERT_TRUE(board.SystemFrame());
    EXPECT_EQ(board.GetPPUMemoryController()->GetWord(0177702, false, false), 0212);
}

TEST(SdlKeyboard, ReleasesAllOnFocusLossAndCanRestart)
{
    sdl_keyboard::Keyboard keyboard;
    ASSERT_TRUE(keyboard.Transition(SDL_SCANCODE_A, true, false, false, true));
    ASSERT_TRUE(keyboard.Transition(SDL_SCANCODE_LSHIFT, true, false, false, true));
    EXPECT_FALSE(keyboard.Transition(SDL_SCANCODE_RSHIFT, true, false, false, true));
    const auto releases = keyboard.ReleaseAll();
    ASSERT_EQ(releases.size(), 2u);
    EXPECT_EQ(releases[0].scan, 0072); EXPECT_FALSE(releases[0].pressed);
    EXPECT_EQ(releases[1].scan, 0105); EXPECT_FALSE(releases[1].pressed);
    EXPECT_TRUE(keyboard.ReleaseAll().empty());
    EXPECT_FALSE(keyboard.Transition(SDL_SCANCODE_RSHIFT, false, false, false, false));
    EXPECT_TRUE(keyboard.Transition(SDL_SCANCODE_A, true, false, false, false));
    EXPECT_FALSE(keyboard.Transition(static_cast<SDL_Scancode>(-1), true, false, false, false));
    EXPECT_FALSE(keyboard.Transition(SDL_SCANCODE_COUNT, true, false, false, false));
}

TEST(SdlKeyboard, MappedTransitionsReachMotherboardKeyboard)
{
    struct Runtime { Runtime() { CProcessor::Init(); } ~Runtime() { CProcessor::Done(); } } runtime;
    {
        CMotherboard board;
        board.Reset();
        for (int column = 0; column < 8; ++column)
            board.KeyboardEvent(static_cast<std::uint8_t>((column << 4) | 3), false);
        board.GetCPU()->SetDCLOPin(true);
        board.GetPPU()->SetDCLOPin(true);
        sdl_keyboard::Keyboard keyboard;
        const auto press = keyboard.Transition(SDL_SCANCODE_A, true, false, false, true);
        ASSERT_TRUE(press);
        board.KeyboardEvent(press->scan, press->pressed);
        ASSERT_TRUE(board.SystemFrame());
        EXPECT_EQ(board.GetPPUMemoryController()->GetWord(0177702, false, false), 0072);
        const auto release = keyboard.Transition(SDL_SCANCODE_A, false, false, false, false);
        ASSERT_TRUE(release);
        board.KeyboardEvent(release->scan, release->pressed);
        ASSERT_TRUE(board.SystemFrame());
        EXPECT_EQ(board.GetPPUMemoryController()->GetWord(0177702, false, false), 0212);
    }
}

TEST(SdlKeyboard, DeliveryPreservesShortPressesAndReleasesOnlyDeliveredKeysOnReset)
{
    sdl_keyboard::Keyboard keyboard;
    sdl_keyboard::Delivery delivery;
    const auto press = keyboard.Transition(SDL_SCANCODE_A, true, false, false, true);
    const auto release = keyboard.Transition(SDL_SCANCODE_A, false, false, false, true);
    ASSERT_TRUE(press); ASSERT_TRUE(release);
    delivery.Push(*press);
    delivery.Push(*release);
    auto event = delivery.Next();
    ASSERT_TRUE(event); EXPECT_TRUE(event->pressed); EXPECT_EQ(event->scan, 0072);
    event = delivery.Next();
    ASSERT_TRUE(event); EXPECT_FALSE(event->pressed); EXPECT_EQ(event->scan, 0072);
    EXPECT_FALSE(delivery.Next());
    delivery.Push({0105, true});
    delivery.Push({0072, true});
    ASSERT_TRUE(delivery.Next());
    const auto releases = delivery.Reset();
    ASSERT_EQ(releases.size(), 1u);
    EXPECT_EQ(releases[0].scan, 0105); EXPECT_FALSE(releases[0].pressed);
    EXPECT_FALSE(delivery.Next());
    EXPECT_TRUE(delivery.Reset().empty());
}
