/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "FrameControl.h"
#include "BreakpointSet.h"
#include "emubase/Board.h"
#include "emubase/Processor.h"

namespace frame_control {
void PrepareRun(CMotherboard& board, const BreakpointSet& cpu, const BreakpointSet& ppu)
{
    if (!cpu.Addresses().empty() || !ppu.Addresses().empty())
    {
        board.GetCPU()->ClearInternalTick();
        board.GetPPU()->ClearInternalTick();
    }
}

void CancelTemporaryBreakpoints(BreakpointSet& cpu, BreakpointSet& ppu)
{
    cpu.SetTemporary(BreakpointSet::NoBreakpoint);
    ppu.SetTemporary(BreakpointSet::NoBreakpoint);
}

bool RunFrame(CMotherboard& board, BreakpointSet& cpu, BreakpointSet& ppu)
{
    board.SetCPUBreakpoints(cpu.Addresses());
    board.SetPPUBreakpoints(ppu.Addresses());
    if (board.SystemFrame()) return true;
    CancelTemporaryBreakpoints(cpu, ppu);
    return false;
}
}
