/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once

namespace floppy_testing
{
struct ReadObserver
{
    void* context;
    // Called by either reader. False injects a failed read.
    bool (*read)(void* context, long offset, bool speculative);
};

// Keep the observer alive until the floppy controller has drained its reader.
void ObserveReadAhead(const ReadObserver* observer);
}
