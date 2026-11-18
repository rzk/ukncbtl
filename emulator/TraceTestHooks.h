/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once

// Implemented only in test-enabled builds.
namespace preview_debugger::testing
{
void FailWorkerTraceWrites(bool fail);
// Throw on the Nth trace record; zero disables the injection.
void ThrowOnTraceRecord(unsigned count);
}
