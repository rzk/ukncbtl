/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once

class BreakpointSet;
class CMotherboard;

namespace frame_control {
// Clear residual instruction timing before starting with breakpoints enabled.
void PrepareRun(CMotherboard& board, const BreakpointSet& cpu, const BreakpointSet& ppu);
void CancelTemporaryBreakpoints(BreakpointSet& cpu, BreakpointSet& ppu);
// Returns false on a breakpoint; temporary targets are cleared on interruption.
// The board borrows breakpoint storage, so both sets must outlive its next frame.
[[nodiscard]] bool RunFrame(CMotherboard& board, BreakpointSet& cpu, BreakpointSet& ppu);
}
