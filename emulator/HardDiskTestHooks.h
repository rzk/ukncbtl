/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once

namespace hard_disk_testing
{
struct ReadObserver
{
    void* context;
    // Called on the read worker. False injects a failed read.
    bool (*read)(void* context, long offset);
};

// Keep the observer alive until all observed drives have drained their reads.
void ObserveReadAhead(const ReadObserver* observer);
}
