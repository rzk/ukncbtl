/*  This file is part of UKNCBTL.
    UKNCBTL is free software: you can redistribute it and/or modify it under the terms
of the GNU Lesser General Public License as published by the Free Software Foundation,
either version 3 of the License, or (at your option) any later version.
    UKNCBTL is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
See the GNU Lesser General Public License for more details.
    You should have received a copy of the GNU Lesser General Public License along with
UKNCBTL. If not, see <http://www.gnu.org/licenses/>. */

// Emulator.cpp

#include "stdafx.h"
#include "RamChanges.h"
#include "FrameControl.h"
#include "FrameTiming.h"
#include "PrinterOutput.h"
#include "RomFile.h"
#include "util/Profiler.h"
#include <algorithm>
#include <array>
#include <format>
#include <memory>
#include <vector>
#include "Main.h"
#include "Emulator.h"
#include "Views.h"
#include "emubase/Emubase.h"
#include "Snapshot.h"
#include "VideoDecoder.h"
#include "SoundGen.h"

//NOTE: I know, we use unsafe string functions
#pragma warning( disable: 4996 )

//////////////////////////////////////////////////////////////////////


CMotherboard* g_pBoard = nullptr;
static std::unique_ptr<CMotherboard> g_boardOwner;

bool g_okEmulatorInitialized = false;
bool g_okEmulatorRunning = false;

static BreakpointSet cpuBreakpoints;
static BreakpointSet ppuBreakpoints;

bool m_okEmulatorSound = false;
uint16_t m_wEmulatorSoundSpeed = 100;
int m_nEmulatorSoundChanges = 0;

bool m_okEmulatorParallel = false;
static PrinterOutput m_parallelOutput;
static void Emulator_ReportPrinterFailure();

static frame_timing::Tracker frameTiming(FRAMERATE);

uint8_t* g_pEmulatorRam[3] = { nullptr, nullptr, nullptr };  // RAM values - for change tracking
uint8_t* g_pEmulatorChangedRam[3] = { nullptr, nullptr, nullptr };  // RAM change flags
static std::unique_ptr<uint8_t[]> g_emulatorRamOwners[3];
static std::unique_ptr<uint8_t[]> g_emulatorChangedRamOwners[3];
uint16_t g_wEmulatorCpuPC = 0177777;      // Current PC value
uint16_t g_wEmulatorPrevCpuPC = 0177777;  // Previous PC value
uint16_t g_wEmulatorPpuPC = 0177777;      // Current PC value
uint16_t g_wEmulatorPrevPpuPC = 0177777;  // Previous PC value

// Digit keys scan codes used for AutoBoot feature
const BYTE m_arrDigitKeyScans[] =
{
    0176, 0030, 0031, 0032, 0013, 0034, 0035, 0016, 0017, 0177  // 0, 1, ... 9
};


//////////////////////////////////////////////////////////////////////


const LPCTSTR FILE_NAME_UKNC_ROM = _T("uknc_rom.bin");

bool Emulator_LoadUkncRom()
{
    std::array<uint8_t, 32768> data{};

    const auto external = rom_file::ReadPrefix(std::filesystem::path(FILE_NAME_UKNC_ROM), std::span{data}.first(32256));
    if (!external && external.error() != rom_file::Error::open_failed) return false;
    if (!external)  // External ROM could not be opened; use the embedded resource.
    {
        HRSRC hRes = nullptr;
        DWORD dwDataSize = 0;
        HGLOBAL hResLoaded = nullptr;
        void * pResData = nullptr;
        if ((hRes = ::FindResource(nullptr, MAKEINTRESOURCE(IDR_UKNC_ROM), _T("BIN"))) == nullptr ||
            (dwDataSize = ::SizeofResource(nullptr, hRes)) < 32256 ||
            (hResLoaded = ::LoadResource(nullptr, hRes)) == nullptr ||
            (pResData = ::LockResource(hResLoaded)) == nullptr)
        {
            return false;
        }
        std::copy_n(static_cast<const uint8_t*>(pResData), 32256, data.begin());
    }

    g_pBoard->LoadROM(data);

    return true;
}

void CALLBACK Emulator_Instruction_Callback(const CProcessor* pProc, uint16_t address, uint16_t opcode);
void CALLBACK Emulator_Interrupt_Callback(const CProcessor* pProc, uint16_t oldPc, uint16_t vector);

bool Emulator_Init()
{
    ASSERT(g_pBoard == nullptr);

    std::ranges::fill(g_pEmulatorRam, nullptr);
    std::ranges::fill(g_pEmulatorChangedRam, nullptr);
    CProcessor::Init();

    cpuBreakpoints.Clear();
    ppuBreakpoints.Clear();
    for (int i = 0; i < MAX_BREAKPOINTCOUNT; ++i)
    {
        cpuBreakpoints.Add(Settings_GetDebugBreakpoint(i, TRUE));
        ppuBreakpoints.Add(Settings_GetDebugBreakpoint(i, FALSE));
    }
    g_boardOwner = std::make_unique<CMotherboard>();
    g_pBoard = g_boardOwner.get();

    if (! Emulator_LoadUkncRom())
    {
        AlertWarning(_T("Failed to load UKNC ROM image."));
        g_boardOwner.reset();
        g_pBoard = nullptr;
        CProcessor::Done();
        return false;
    }

    g_pBoard->SetNetStation((uint16_t)Settings_GetNetStation());

    g_pBoard->SetInstructionCallback(Emulator_Instruction_Callback);
    g_pBoard->SetInterruptCallback(Emulator_Interrupt_Callback);

    g_pBoard->Reset();
    Emulator_ClearHistory();

    if (m_okEmulatorSound)
    {
        SoundGen_Initialize(Settings_GetSoundVolume());
        g_pBoard->SetSoundGenCallback(SoundGen_FeedDAC);
    }

    frameTiming.ResetUptime();

    // Allocate memory for old RAM values
    for (int i = 0; i < 3; i++)
    {
        g_emulatorRamOwners[i] = std::make_unique<uint8_t[]>(65536);
        g_emulatorChangedRamOwners[i] = std::make_unique<uint8_t[]>(65536);
        g_pEmulatorRam[i] = g_emulatorRamOwners[i].get();
        g_pEmulatorChangedRam[i] = g_emulatorChangedRamOwners[i].get();
    }

    g_okEmulatorInitialized = TRUE;
    return true;
}

void Emulator_Done()
{
    ASSERT(g_pBoard != nullptr);

    // Temporary run-to targets are not persistent debugger settings.
    cpuBreakpoints.SetTemporary(BreakpointSet::NoBreakpoint);
    ppuBreakpoints.SetTemporary(BreakpointSet::NoBreakpoint);
    for (int i = 0; i < MAX_BREAKPOINTCOUNT; ++i)
    {
        Settings_SetDebugBreakpoint(i, TRUE, cpuBreakpoints.Data()[i]);
        Settings_SetDebugBreakpoint(i, FALSE, ppuBreakpoints.Data()[i]);
    }
    CProcessor::Done();

    g_pBoard->SetInstructionCallback(nullptr);
    g_pBoard->SetInterruptCallback(nullptr);
    g_pBoard->SetSoundGenCallback(nullptr);
    SoundGen_Finalize();

    g_pBoard->SetParallelOutCallback(nullptr);
    (void)m_parallelOutput.Close();
    if (m_parallelOutput.TakeError())
        AlertWarning(_T("Failed to write or close the printer output file."));
    m_okEmulatorParallel = false;

    g_boardOwner.reset();
    g_pBoard = nullptr;

    // Free memory used for old RAM values
    for (int i = 0; i < 3; i++)
    {
        g_emulatorRamOwners[i].reset();
        g_emulatorChangedRamOwners[i].reset();
        g_pEmulatorRam[i] = nullptr;
        g_pEmulatorChangedRam[i] = nullptr;
    }

    g_okEmulatorInitialized = false;
}

void Emulator_Start()
{
    g_okEmulatorRunning = TRUE;

    // Set title bar text
    MainWindow_UpdateWindowTitle();
    MainWindow_UpdateMenu();

    frameTiming.Start(frame_timing::Clock::now());

    // For proper breakpoint processing
    frame_control::PrepareRun(*g_pBoard, cpuBreakpoints, ppuBreakpoints);
}
void Emulator_Stop()
{
    g_okEmulatorRunning = false;

    frame_control::CancelTemporaryBreakpoints(cpuBreakpoints, ppuBreakpoints);

    if (m_parallelOutput.IsOpen())
        (void)m_parallelOutput.Flush();
    Emulator_ReportPrinterFailure();

    // Reset title bar message
    MainWindow_UpdateWindowTitle();
    MainWindow_UpdateMenu();

    // Reset FPS indicator
    MainWindow_SetStatusbarText(StatusbarPartFPS, nullptr);

    MainWindow_UpdateAllViews();
}

void Emulator_Reset()
{
    ASSERT(g_pBoard != nullptr);

    g_pBoard->Reset();

    Emulator_ClearHistory();

    frameTiming.ResetUptime();

    MainWindow_UpdateAllViews();
}

bool Emulator_AddCPUBreakpoint(uint16_t address) { return cpuBreakpoints.Add(address); }
bool Emulator_AddPPUBreakpoint(uint16_t address) { return ppuBreakpoints.Add(address); }
bool Emulator_RemoveCPUBreakpoint(uint16_t address) { return cpuBreakpoints.Remove(address); }
bool Emulator_RemovePPUBreakpoint(uint16_t address) { return ppuBreakpoints.Remove(address); }
void Emulator_SetTempCPUBreakpoint(uint16_t address) { cpuBreakpoints.SetTemporary(address); }
void Emulator_SetTempPPUBreakpoint(uint16_t address) { ppuBreakpoints.SetTemporary(address); }
const uint16_t* Emulator_GetCPUBreakpointList() { return cpuBreakpoints.Data(); }
const uint16_t* Emulator_GetPPUBreakpointList() { return ppuBreakpoints.Data(); }
bool Emulator_IsBreakpoint()
{
    return cpuBreakpoints.Contains(g_pBoard->GetCPU()->GetPC()) ||
        ppuBreakpoints.Contains(g_pBoard->GetPPU()->GetPC());
}
bool Emulator_IsBreakpoint(bool okCpuPpu, uint16_t address)
{
    return (okCpuPpu ? cpuBreakpoints : ppuBreakpoints).Contains(address);
}
void Emulator_RemoveAllBreakpoints(bool okCpuPpu)
{
    (okCpuPpu ? cpuBreakpoints : ppuBreakpoints).Clear();
}

bool Emulator_IsSound()
{
    return m_nEmulatorSoundChanges > 0;
}

void Emulator_SetSpeed(uint16_t realspeed)
{
    uint16_t speedpercent;
    switch (realspeed)
    {
    case 0: speedpercent = 500; break;
    case 1: speedpercent = 100; break;
    case 2: speedpercent = 200; break;
    case 3: speedpercent = 400; break;
    case 0x7fff: speedpercent = 50; break;
    case 0x7ffe: speedpercent = 25; break;
    case 0x7ffd: speedpercent = 10; break;
    default: speedpercent = 100; break;
    }
    m_wEmulatorSoundSpeed = speedpercent;

    if (m_okEmulatorSound)
        SoundGen_SetSpeed(m_wEmulatorSoundSpeed);
}

void Emulator_SetSound(bool soundOnOff)
{
    if (m_okEmulatorSound != soundOnOff)
    {
        if (soundOnOff)
        {
            SoundGen_Initialize(Settings_GetSoundVolume());
            SoundGen_SetSpeed(m_wEmulatorSoundSpeed);
            g_pBoard->SetSoundGenCallback(SoundGen_FeedDAC);
        }
        else
        {
            g_pBoard->SetSoundGenCallback(nullptr);
            SoundGen_Finalize();
        }
    }

    m_okEmulatorSound = soundOnOff;
}

void Emulator_SetSoundAY(bool soundAYOnOff)
{
    g_pBoard->SetSoundAY(soundAYOnOff);
}

void Emulator_SetSoundCovox(bool soundCovoxOnOff)
{
    g_pBoard->SetSoundCovox(soundCovoxOnOff);
}

void Emulator_SetMouse(bool mouseOnOff)
{
    g_pBoard->SetMouse(mouseOnOff);
}

static InstructionHistory instructionHistory;

int Emulator_GetHistoryLength(bool okCpuPpu)
{
    return instructionHistory.Length(okCpuPpu);
}

const InstructionRecord& Emulator_GetHistoryRecord(bool okCpuPpu, int index)
{
    return instructionHistory.Get(okCpuPpu, index);
}

void Emulator_ClearHistory()
{
    instructionHistory.Clear();
}

LPCTSTR Emulator_GetInterruptName(uint16_t vector)
{
    return InstructionHistory::InterruptName(vector);
}

// Called by CPU/PPU before each instruction is executed; installed always, records history and writes trace
void CALLBACK Emulator_Instruction_Callback(const CProcessor* pProc, uint16_t address, uint16_t opcode)
{
    bool okCpu = (pProc == g_pBoard->GetCPU());

    // Instruction is at most 3 words long
    const CMemoryController* pMemCtl = pProc->GetMemoryControllerConst();
    bool okHaltMode = pProc->IsHaltMode();
    std::array<uint16_t, 3> memory{};
    memory[0] = opcode;
    memory[1] = pMemCtl->GetWordView(address + 1 * 2, okHaltMode, true).value;
    memory[2] = pMemCtl->GetWordView(address + 2 * 2, okHaltMode, true).value;

    instructionHistory.RecordInstruction(okCpu, address, memory, pProc->GetPSW());

#if !defined(PRODUCT)
    uint32_t dwTrace = g_pBoard->GetTrace();
    uint32_t dwMask = okCpu ? TRACE_CPU : TRACE_PPU;
    if ((dwTrace & dwMask) == 0)
        return;

    TCHAR bufaddr[7];
    PrintOctalValue(bufaddr, address);

    const auto decoded = DisassembleInstruction(memory, address);
    if (!decoded) return;
    TCHAR buffer[64];
    FormatBuffer(buffer, _T("{}\t{}\t{}\t{}\r\n"), pProc->GetName(), bufaddr, decoded->mnemonic.c_str(), decoded->argument.c_str());

    DebugLog(buffer);
#endif // !PRODUCT
}

// Called by CPU/PPU when an interrupt has been taken, the new PC/PSW are already loaded;
// installed always, records history and writes trace
void CALLBACK Emulator_Interrupt_Callback(const CProcessor* pProc, uint16_t oldPc, uint16_t vector)
{
    bool okCpu = (pProc == g_pBoard->GetCPU());

    instructionHistory.RecordInterrupt(okCpu, oldPc, vector, pProc->GetPC(), pProc->GetPSW());

#if !defined(PRODUCT)
    uint32_t dwTrace = g_pBoard->GetTrace();
    uint32_t dwMask = okCpu ? TRACE_CPUINT : TRACE_PPUINT;
    if ((dwTrace & dwMask) == 0)
        return;

    TCHAR bufaddr[7], bufvector[7], bufnewpc[7];
    PrintOctalValue(bufaddr, oldPc);
    PrintOctalValue(bufvector, vector);
    PrintOctalValue(bufnewpc, pProc->GetPC());

    TCHAR buffer[80];
    FormatBuffer(buffer, _T("{}\t{}\tINT\t{}\t{}\t{}\r\n"),
            pProc->GetName(), bufaddr, bufvector, Emulator_GetInterruptName(vector), bufnewpc);

    DebugLog(buffer);
#endif // !PRODUCT
}

#if !defined(PRODUCT)

void Emulator_SetTrace(uint32_t dwTrace)
{
    g_pBoard->SetTrace(dwTrace);
}

#endif // !PRODUCT

bool CALLBACK Emulator_ParallelOut_Callback(BYTE byte)
{
    return m_parallelOutput.Write(byte);
}

static void Emulator_ReportPrinterFailure()
{
    if (!m_parallelOutput.TakeError()) return;
    g_pBoard->SetParallelOutCallback(nullptr);
    (void)m_parallelOutput.Close();
    (void)m_parallelOutput.TakeError(); // Report a related close failure only once.
    m_okEmulatorParallel = false;
    Settings_SetParallel(FALSE);
    MainWindow_UpdateMenu();
    AlertWarning(_T("Printer output failed. Parallel output has been disabled."));
}

bool Emulator_SetParallel(bool parallelOnOff)
{
    if (m_okEmulatorParallel == parallelOnOff)
        return true;

    if (!parallelOnOff)
    {
        g_pBoard->SetParallelOutCallback(nullptr);
        (void)m_parallelOutput.Close();
        m_okEmulatorParallel = false;
        if (m_parallelOutput.TakeError())
        {
            AlertWarning(_T("Failed to write or close the printer output file."));
            return false;
        }
    }
    else
    {
        if (!m_parallelOutput.Open("printer.log"))
        {
            AlertWarning(_T("Failed to open the printer output file."));
            return false;
        }
        g_pBoard->SetParallelOutCallback(Emulator_ParallelOut_Callback);
    }

    m_okEmulatorParallel = parallelOnOff;
    return true;
}

bool Emulator_SystemFrame()
{
    UKNC_PROFILE_SCOPE("Emulator frame");
    SoundGen_SetVolume(Settings_GetSoundVolume());

    ScreenView_ScanKeyboard();
    if (Settings_GetMouse())
        ScreenView_UpdateMouse();

    const bool frameCompleted = frame_control::RunFrame(*g_pBoard, cpuBreakpoints, ppuBreakpoints);
    Emulator_ReportPrinterFailure();
    if (!frameCompleted)
        return false;

    // Calculate frames per second
    const auto timing = frameTiming.OnFrame(frame_timing::Clock::now());
    if (timing.speedPercent)
    {
        TCHAR buffer[16];
        FormatBuffer(buffer, _T("{:03.0f}%"), *timing.speedPercent);
        MainWindow_SetStatusbarText(StatusbarPartFPS, buffer);

        bool floppyEngine = g_pBoard->IsFloppyEngineOn();
        MainWindow_SetStatusbarText(StatusbarPartFloppyEngine, floppyEngine ? _T("Motor") : nullptr);

    }

    // Calculate emulator uptime
    if (timing.uptimeAdvanced)
    {
        int seconds = (int) (frameTiming.Uptime() % 60);
        int minutes = (int) (frameTiming.Uptime() / 60 % 60);
        int hours   = (int) (frameTiming.Uptime() / 3600 % 60);

        TCHAR buffer[20];
        FormatBuffer(buffer, _T("Uptime: {:02}:{:02}:{:02}"), hours, minutes, seconds);
        MainWindow_SetStatusbarText(StatusbarPartUptime, buffer);
    }

    // Update "Sound" indicator every 5 frames
    m_nEmulatorSoundChanges += g_pBoard->GetSoundChanges();
    if (frameTiming.FramesWithinSecond() % 5 == 0)
    {
        bool soundOn = m_nEmulatorSoundChanges > 0;
        MainWindow_SetStatusbarText(StatusbarPartSound, soundOn ? _T("Sound") : nullptr);
        m_nEmulatorSoundChanges = 0;
    }

    // Auto-boot option processing: select boot menu item and press Enter
    if (Option_AutoBoot > 0)
    {
        BYTE digitKeyScan = m_arrDigitKeyScans[Option_AutoBoot];
        if (frameTiming.Uptime() == 2 && frameTiming.FramesWithinSecond() == 6)
            ScreenView_KeyEvent(digitKeyScan, TRUE);  // Press the digit key
        else if (frameTiming.Uptime() == 2 && frameTiming.FramesWithinSecond() == 10)
            ScreenView_KeyEvent(digitKeyScan, FALSE);  // Release the digit key
        else if (frameTiming.Uptime() == 2 && frameTiming.FramesWithinSecond() == 16)
            ScreenView_KeyEvent(0153, TRUE);  // Press "Enter"
        else if (frameTiming.Uptime() == 2 && frameTiming.FramesWithinSecond() == 20)
        {
            ScreenView_KeyEvent(0153, FALSE);  // Release "Enter"
            Option_AutoBoot = 0;  // All done
        }
    }

    return true;
}

uint32_t Emulator_GetUptime()
{
    return frameTiming.Uptime();
}

// Update cached values after Run or Step
void Emulator_OnUpdate()
{
    // Update stored PC value
    g_wEmulatorPrevCpuPC = g_wEmulatorCpuPC;
    g_wEmulatorCpuPC = g_pBoard->GetCPU()->GetPC();
    g_wEmulatorPrevPpuPC = g_wEmulatorPpuPC;
    g_wEmulatorPpuPC = g_pBoard->GetPPU()->GetPC();

    // Update memory change flags
    for (int plane = 0; plane < 3; plane++)
    {
        ram_changes::Update(std::span<uint8_t>(g_pEmulatorRam[plane], 65536),
            std::span<uint8_t>(g_pEmulatorChangedRam[plane], 65536),
            [plane](uint16_t address) { return g_pBoard->GetRAMByte(plane, address); });
    }
}

// Get RAM change flag for RAM word
//   addrtype - address mode - see ADDRTYPE_XXX constants
uint16_t Emulator_GetChangeRamStatus(int addrtype, uint16_t address)
{
    switch (addrtype)
    {
    case ADDRTYPE_RAM0:
    case ADDRTYPE_RAM1:
    case ADDRTYPE_RAM2:
        if (!g_pEmulatorChangedRam[addrtype]) return 0;
        return ram_changes::Word(std::span<const uint8_t>(g_pEmulatorChangedRam[addrtype], 65536), address);
    case ADDRTYPE_RAM12:
        if (!g_pEmulatorChangedRam[1] || !g_pEmulatorChangedRam[2]) return 0;
        return ram_changes::Interleaved(std::span<const uint8_t>(g_pEmulatorChangedRam[1], 65536),
            std::span<const uint8_t>(g_pEmulatorChangedRam[2], 65536), address);
    default:
        return 0;
    }
}

bool Emulator_LoadROMCartridge(int slot, LPCTSTR sFilePath)
{
    if (!sFilePath || !*sFilePath)
    {
        AlertWarning(_T("Failed to load ROM cartridge image."));
        return false;
    }
    std::array<BYTE, 24 * 1024> image{};
    const auto loaded = rom_file::ReadPrefix(std::filesystem::path(sFilePath), image);
    if (!loaded)
    {
        if (loaded.error() == rom_file::Error::open_failed)
            AlertWarning(_T("Failed to load ROM cartridge image."));
        return false;
    }
    g_pBoard->LoadROMCartridge(slot, std::span<const uint8_t, 24 * 1024>{image});

    //TODO: Save the file name for a future SaveImage() call

    return true;
}

void Emulator_PrepareScreenRGB32(std::span<uint32_t> pixels, std::span<const uint32_t> colors)
{
    if (!g_okEmulatorInitialized) return;
    video::RenderRgb(*g_pBoard, pixels, colors);
}

void Emulator_PrepareScreenToText(std::span<uint32_t> pixels, std::span<const uint32_t> colors)
{
    if (!g_okEmulatorInitialized) return;
    video::RenderText(*g_pBoard, pixels, colors);
}

//////////////////////////////////////////////////////////////////////
//
// Emulator image format - see CMotherboard::SaveToImage()
// Image header format (32 bytes):
//   4 bytes        UKNC_IMAGE_HEADER1
//   4 bytes        UKNC_IMAGE_HEADER2
//   4 bytes        UKNC_IMAGE_VERSION
//   4 bytes        UKNC_IMAGE_SIZE
//   4 bytes        UKNC uptime
//   12 bytes       Not used
//TODO: 256 bytes * 2 - Cartridge 1..2 path
//TODO: 256 bytes * 4 - Floppy 1..4 path
//TODO: 256 bytes * 2 - Hard 1..2 path

bool Emulator_SaveImage(LPCTSTR sFilePath)
{
    return snapshot::Save(sFilePath, *g_pBoard, frameTiming.Uptime());
}

bool Emulator_LoadImage(LPCTSTR sFilePath)
{
    auto uptime = frameTiming.Uptime();
    if (!snapshot::Load(sFilePath, *g_pBoard, uptime, instructionHistory, Emulator_Stop))
        return false;
    frameTiming.SetUptime(uptime);
    return true;
}

//////////////////////////////////////////////////////////////////////
