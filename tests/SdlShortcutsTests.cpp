/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "SdlShortcuts.h"
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_scancode.h>
#include <gtest/gtest.h>

TEST(SdlShortcuts, RecognizesLegacyKeysAndRejectsUnrelatedModifiersAndRepeats)
{
    SDL_KeyboardEvent key{};
    key.type = SDL_EVENT_KEY_DOWN;
    key.scancode = SDL_SCANCODE_F8;
    EXPECT_EQ(sdl_shortcuts::Classify(key), sdl_shortcuts::Action::step);
    key.mod = SDL_KMOD_LSHIFT;
    EXPECT_EQ(sdl_shortcuts::Classify(key), sdl_shortcuts::Action::stepOver);
    key.mod = SDL_KMOD_CTRL;
    EXPECT_FALSE(sdl_shortcuts::Classify(key));
    key.mod = SDL_KMOD_NONE; key.repeat = true;
    EXPECT_FALSE(sdl_shortcuts::Classify(key));
    key.repeat = false; key.type = SDL_EVENT_KEY_UP;
    EXPECT_FALSE(sdl_shortcuts::Classify(key));
    key.type = SDL_EVENT_KEY_DOWN; key.scancode = SDL_SCANCODE_F10;
    EXPECT_EQ(sdl_shortcuts::Classify(key), sdl_shortcuts::Action::fullscreen);
    key.scancode = SDL_SCANCODE_MEDIA_PLAY_PAUSE;
    EXPECT_EQ(sdl_shortcuts::Classify(key), sdl_shortcuts::Action::toggleRun);
    key.scancode = SDL_SCANCODE_MEDIA_STOP;
    EXPECT_EQ(sdl_shortcuts::Classify(key), sdl_shortcuts::Action::reset);
}

TEST(SdlShortcuts, StepsOnlyWhilePausedAndRespectsCaptureAndDialogs)
{
    using sdl_shortcuts::Action;
    EXPECT_TRUE(sdl_shortcuts::Allowed(Action::step, false, false, false));
    EXPECT_FALSE(sdl_shortcuts::Allowed(Action::stepOver, true, false, false));
    EXPECT_TRUE(sdl_shortcuts::Allowed(Action::toggleRun, true, false, false));
    EXPECT_FALSE(sdl_shortcuts::Allowed(Action::toggleRun, true, true, false));
    EXPECT_FALSE(sdl_shortcuts::Allowed(Action::fullscreen, false, false, true));
}
