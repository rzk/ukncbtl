/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once

#include <SDL3/SDL_thread.h>

// Instantiate as thread_local on a C++ worker that calls SDL.
struct SdlThreadCleanup
{
    ~SdlThreadCleanup() { SDL_CleanupTLS(); }
};
