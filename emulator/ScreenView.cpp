/*  This file is part of UKNCBTL.
    UKNCBTL is free software: you can redistribute it and/or modify it under the terms
of the GNU Lesser General Public License as published by the Free Software Foundation,
either version 3 of the License, or (at your option) any later version.
    UKNCBTL is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
See the GNU Lesser General Public License for more details.
    You should have received a copy of the GNU Lesser General Public License along with
UKNCBTL. If not, see <http://www.gnu.org/licenses/>. */

// ScreenView.cpp

#include "stdafx.h"
#include "util/Profiler.h"
#include <algorithm>
#include <vector>
#include <memory>
#include <windowsx.h>
#include "Input.h"
#include "ScreenText.h"
#include "ScreenshotImage.h"
#include "VideoPalette.h"
#ifdef UKNCBTL_SDL3_PNG
#include "ScreenshotFile.h"
#endif
#include "Main.h"
#include "Views.h"
#include "Emulator.h"
#include "util/BitmapFile.h"
#include "util/ScreenshotFormat.h"
#include "util/ScreenScale.h"
#include "emubase/Emubase.h"

//////////////////////////////////////////////////////////////////////


HWND g_hwndScreen = nullptr;  // Screen View window handle

uint32_t* m_bits = nullptr;
static std::unique_ptr<uint32_t[]> m_screenBitsOwner;
int m_cxScreenWidth = UKNC_SCREEN_WIDTH;
int m_cyScreenHeight = UKNC_SCREEN_HEIGHT;
int m_xScreenOffset = 0;
int m_yScreenOffset = 0;
ScreenViewMode m_ScreenMode = RGBScreen;
int m_ScreenHeightMode = 0;  // Render mode

static bool g_renderInitialized = false;
static std::vector<uint32_t> g_renderPixels;
HMENU g_hScreenModeMenu = nullptr;
int g_nScreenModeIndex = 0;

BYTE m_ScreenKeyState[256];
input::KeyQueue m_ScreenKeyQueue;
void ScreenView_PutKeyEventToQueue(WORD keyevent);
WORD ScreenView_GetKeyEventFromQueue();

POINT m_LastMousePos;

bool bEnter = false;
bool bNumpadEnter = false;
bool bEntPressed = false;

void ScreenView_OnDraw(HDC hdc);

void ScreenView_OnRButtonDown(int mousex, int mousey);


//Prototype of the screen conversion function
using PREPARE_SCREEN_CALLBACK = void (CALLBACK*)(void* pImageBits);

struct ScreenModeStruct
{
    int modeNum;
    int width;
    int height;
    TCHAR description[40];
}
static ScreenModeReference[32];

LPCTSTR ScreenView_GetRenderModeDescription(int renderMode)
{
    if (renderMode < 0 || renderMode >= 32)
        return nullptr;
    LPCTSTR modedesc = ScreenModeReference[renderMode].description;
    if (*modedesc == 0) return nullptr;  // Empty string
    return modedesc;
}


//////////////////////////////////////////////////////////////////////


void ScreenView_RegisterClass()
{
    WNDCLASSEX wcex;
    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style          = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc    = ScreenViewWndProc;
    wcex.cbClsExtra     = 0;
    wcex.cbWndExtra     = 0;
    wcex.hInstance      = g_hInst;
    wcex.hIcon          = nullptr;
    wcex.hCursor        = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground  = nullptr; //(HBRUSH)(COLOR_WINDOW+1);
    wcex.lpszMenuName   = nullptr;
    wcex.lpszClassName  = CLASSNAME_SCREENVIEW;
    wcex.hIconSm        = nullptr;

    RegisterClassEx(&wcex);
}

void ScreenView_Init()
{
    m_screenBitsOwner = std::make_unique<uint32_t[]>(UKNC_SCREEN_WIDTH * UKNC_SCREEN_HEIGHT);
    m_bits = m_screenBitsOwner.get();

    ::GetCursorPos(&m_LastMousePos);
}

void ScreenView_Done()
{
    m_screenBitsOwner.reset();
    m_bits = nullptr;
}

ScreenViewMode ScreenView_GetMode()
{
    return m_ScreenMode;
}
void ScreenView_SetMode(ScreenViewMode newMode)
{
    m_ScreenMode = newMode;
}

BOOL ScreenView_InitRender()
{
    std::ranges::fill(ScreenModeReference, ScreenModeStruct{});
    g_nScreenModeIndex = 0;
    for (const auto& mode : screen_scale::Modes())
    {
        auto& entry = ScreenModeReference[g_nScreenModeIndex];
        entry.modeNum = g_nScreenModeIndex;
        entry.width = mode.width;
        entry.height = mode.height;
        _tcsncpy_s(entry.description, _countof(entry.description), mode.description, _TRUNCATE);
        ++g_nScreenModeIndex;
    }
    g_hScreenModeMenu = nullptr;
    MainWindow_UpdateRenderModeMenu();
    g_renderInitialized = true;
    const auto& mode = screen_scale::GetMode(m_ScreenHeightMode);
    m_cxScreenWidth = mode.width;
    m_cyScreenHeight = mode.height;
    g_renderPixels.resize(static_cast<size_t>(mode.width) * mode.height);
    return TRUE;
}

void ScreenView_DoneRender()
{
    g_renderInitialized = false;
    g_renderPixels.clear();
}

// Create Screen View as child of Main Window
void ScreenView_Create(HWND hwndParent, int x, int y)
{
    ASSERT(hwndParent != nullptr);

    int xLeft = x;
    int yTop = y;
    int cxWidth = m_cxScreenWidth;
    int cyScreenHeight = m_cyScreenHeight;
    int cyHeight = cyScreenHeight;

    g_hwndScreen = CreateWindow(
            CLASSNAME_SCREENVIEW, nullptr,
            WS_CHILD | WS_VISIBLE,
            xLeft, yTop, cxWidth, cyHeight,
            hwndParent, nullptr, g_hInst, nullptr);

    // Initialize m_ScreenKeyState
    VERIFY(::GetKeyboardState(m_ScreenKeyState));
}

LRESULT CALLBACK ScreenViewWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_COMMAND:
        ::PostMessage(g_hwnd, WM_COMMAND, wParam, lParam);
        break;
    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);

            ScreenView_PrepareScreen();
            ScreenView_OnDraw(hdc);

            EndPaint(hWnd, &ps);
        }
        break;
    case WM_LBUTTONDOWN:
        SetFocus(hWnd);
        break;
    case WM_RBUTTONDOWN:
        ScreenView_OnRButtonDown(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        break;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (wParam == VK_RETURN)
        {
            if (lParam & 0x1000000)
                bNumpadEnter = TRUE;
            else
                bEnter = TRUE;
        }
        break;
    case WM_KEYUP:
    case WM_SYSKEYUP:
        if (wParam == VK_RETURN)
        {
            if (lParam & 0x1000000)
                bNumpadEnter = FALSE;
            else
                bEnter = FALSE;
        }
        break;
    case WM_SETCURSOR:
        if (::GetFocus() == g_hwndScreen)
        {
            SetCursor(::LoadCursor(nullptr, MAKEINTRESOURCE(IDC_IBEAM)));
            return (LRESULT) TRUE;
        }
        return DefWindowProc(hWnd, message, wParam, lParam);
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return (LRESULT)FALSE;
}

void ScreenView_OnRButtonDown(int mousex, int mousey)
{
    ::SetFocus(g_hwndScreen);

    if (Settings_GetMouse())
        return;

    HMENU hMenu = ::CreatePopupMenu();
    ::AppendMenu(hMenu, 0, ID_FILE_SCREENSHOT, _T("Screenshot"));
    ::AppendMenu(hMenu, 0, ID_FILE_SCREENSHOTTOCLIPBOARD, _T("Screenshot to Clipboard"));
    ::AppendMenu(hMenu, 0, ID_FILE_SCREENTOCLIPBOARD, _T("Screen Text to Clipboard"));
    if (MainWindow_IsFullscreen())
    {
        ::AppendMenu(hMenu, MF_SEPARATOR, 0, nullptr);
        ::AppendMenu(hMenu, 0, ID_VIEW_FULLSCREEN, _T("Exit Fullscreen"));
    }

    POINT pt = { mousex, mousey };
    ::ClientToScreen(g_hwndScreen, &pt);
    ::TrackPopupMenu(hMenu, 0, pt.x, pt.y, 0, g_hwndScreen, nullptr);

    VERIFY(::DestroyMenu(hMenu));
}

int ScreenView_GetRenderMode()
{
    return m_ScreenHeightMode;
}
void ScreenView_SetRenderMode(int newRenderMode)
{
    if (m_ScreenHeightMode == newRenderMode) return;

    if (!g_renderInitialized || newRenderMode < 0 || newRenderMode >= g_nScreenModeIndex)
        return;

    ScreenModeStruct* pmode = ScreenModeReference + newRenderMode;
    m_cxScreenWidth = pmode->width;
    m_cyScreenHeight = pmode->height;
    g_renderPixels.resize(static_cast<size_t>(pmode->width) * pmode->height);
    m_ScreenHeightMode = newRenderMode;

    ScreenView_RedrawScreen();

    //if (pmode->width > 0 && pmode->height > 0)
    //{
    //    int cxScreen = pmode->width;
    //    int cyScreen = pmode->height;
    //    int cyBorder = ::GetSystemMetrics(SM_CYBORDER);
    //    int cyHeight = cyScreen + cyBorder * 2;
    //    ::SetWindowPos(g_hwndScreen, nullptr, 0,0, cxScreen, cyScreen, SWP_NOZORDER | SWP_NOMOVE);
    //}
}

void ScreenView_OnDraw(HDC hdc)
{
    UKNC_PROFILE_SCOPE("Screen draw");
    if (g_renderInitialized && m_bits && screen_scale::Apply(m_ScreenHeightMode,
        std::span<const uint32_t>(m_bits, UKNC_SCREEN_WIDTH * UKNC_SCREEN_HEIGHT), g_renderPixels))
    {
        RECT client{};
        ::GetClientRect(g_hwndScreen, &client);
        ::FillRect(hdc, &client, static_cast<HBRUSH>(::GetStockObject(BLACK_BRUSH)));
        const int x = std::max(0L, (client.right - m_cxScreenWidth) / 2);
        const int y = std::max(0L, (client.bottom - m_cyScreenHeight) / 2);
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = m_cxScreenWidth;
        info.bmiHeader.biHeight = m_cyScreenHeight;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        ::SetDIBitsToDevice(hdc, x, y, m_cxScreenWidth, m_cyScreenHeight, 0, 0,
            0, m_cyScreenHeight, g_renderPixels.data(), &info, DIB_RGB_COLORS);
    }

    // Calculate m_xScreenOffset and m_yScreenOffset for mouse calculations
    RECT rc;  ::GetClientRect(g_hwndScreen, &rc);
    m_xScreenOffset = m_yScreenOffset = 0;
    if (rc.right > m_cxScreenWidth)
        m_xScreenOffset = (rc.right - m_cxScreenWidth) / 2;
    if (rc.bottom > m_cyScreenHeight)
        m_yScreenOffset = (rc.bottom - m_cyScreenHeight) / 2;
}

void ScreenView_RedrawScreen()
{
    ScreenView_PrepareScreen();

    HDC hdc = GetDC(g_hwndScreen);
    ScreenView_OnDraw(hdc);
    VERIFY(::ReleaseDC(g_hwndScreen, hdc));
}

// Choose color palette depending of screen mode
std::span<const uint32_t, 16 * 8> ScreenView_GetPalette()
{
    switch (m_ScreenMode)
    {
    case GrayScreen: return video_palette::Get(video_palette::Mode::gray);
    case GRBScreen: return video_palette::Get(video_palette::Mode::grb);
    default: return video_palette::Get(video_palette::Mode::rgb);
    }
}

void ScreenView_PrepareScreen()
{
    UKNC_PROFILE_SCOPE("Prepare screen");
    if (m_bits == nullptr) return;

    Emulator_PrepareScreenRGB32(std::span{m_bits, static_cast<size_t>(UKNC_SCREEN_WIDTH * UKNC_SCREEN_HEIGHT)},
        ScreenView_GetPalette());
}

void ScreenView_PutKeyEventToQueue(WORD keyevent) { m_ScreenKeyQueue.Push(keyevent); }
WORD ScreenView_GetKeyEventFromQueue() { return m_ScreenKeyQueue.Pop(); }

void ScreenView_ScanKeyboard()
{
    if (! g_okEmulatorRunning) return;
    if (::GetFocus() == g_hwndScreen)
    {
        // Read current keyboard state
        BYTE keys[256];
        VERIFY(::GetKeyboardState(keys));
        input::NormalizeEnter(keys, bEnter, bNumpadEnter, bEntPressed);
        // Choose the mapping table depending on the RUS/LAT flag in the UKNC
        uint16_t ukncRegister = g_pBoard->GetKeyboardRegister();

        // Check every key for state change
        for (int scan = 0; scan < 256; scan++)
        {
            const WORD event = input::KeyTransition(static_cast<BYTE>(scan), keys[scan],
                    m_ScreenKeyState[scan], (ukncRegister & KEYB_LAT) != 0);
            if (event != 0)
            {
                ScreenView_PutKeyEventToQueue(event);
                KeyboardView_KeyEvent(LOBYTE(event), (event & 0x8000) != 0);
            }
        }
    }

    // Process the keyboard queue
    WORD keyevent;
    while ((keyevent = ScreenView_GetKeyEventFromQueue()) != 0)
    {
        bool pressed = ((keyevent & 0x8000) != 0);
        BYTE ukncscan = LOBYTE(keyevent);
        g_pBoard->KeyboardEvent(ukncscan, pressed);
    }
}

// External key event - e.g. from KeyboardView
void ScreenView_KeyEvent(BYTE keyscan, BOOL pressed)
{
    ScreenView_PutKeyEventToQueue(MAKEWORD(keyscan, pressed ? 128 : 0));
}

void ScreenView_UpdateMouse()
{
    POINT mousepos;
    ::GetCursorPos(&mousepos);

    RECT rcScreen = { m_xScreenOffset, m_yScreenOffset, m_xScreenOffset + m_cxScreenWidth, m_yScreenOffset + m_cyScreenHeight };
    ::MapWindowPoints(g_hwndScreen, nullptr, (LPPOINT)&rcScreen, 2);
    const auto dx = input::MouseAxis(mousepos.x, m_LastMousePos.x, rcScreen.left, rcScreen.right,
            m_cxScreenWidth, UKNC_SCREEN_WIDTH);
    const auto dy = input::MouseAxis(mousepos.y, m_LastMousePos.y, rcScreen.top, rcScreen.bottom,
            m_cyScreenHeight, UKNC_SCREEN_HEIGHT);
    const bool focused = ::GetFocus() == g_hwndScreen;
    const bool btnLeft = input::MouseButton(::GetAsyncKeyState(VK_LBUTTON), focused);
    const bool btnRight = input::MouseButton(::GetAsyncKeyState(VK_RBUTTON), focused);

    g_pBoard->MouseMove((int16_t)dx, (int16_t)dy, btnLeft, btnRight);

    m_LastMousePos = mousepos;
}

BOOL ScreenView_SaveScreenshot(LPCTSTR sFileName, int screenshotMode)
{
    ASSERT(sFileName != nullptr);

    std::vector<uint32_t> bits(UKNC_SCREEN_WIDTH * UKNC_SCREEN_HEIGHT);
    Emulator_PrepareScreenRGB32(bits, ScreenView_GetPalette());

    const auto image = screenshot_image::Prepare(bits, screenshotMode);
    if (!image)
        return FALSE;

#ifdef UKNCBTL_SDL3_PNG
    return screenshot_file::Save(std::filesystem::path(sFileName), *image).has_value();
#else
    const auto imageFormat = screenshot_format::FromPath(std::filesystem::path(sFileName)) == screenshot_format::Format::bmp
        ? BitmapFileFormatBmp : BitmapFileFormatPng;
    BOOL result = BitmapFile_SaveImageFile(
            image->pixels, sFileName, imageFormat, image->width, image->height);

    return result;
#endif
}

HGLOBAL ScreenView_GetScreenshotAsDIB(int screenshotMode)
{
    std::vector<uint32_t> bits(UKNC_SCREEN_WIDTH * UKNC_SCREEN_HEIGHT);

    Emulator_PrepareScreenRGB32(bits, ScreenView_GetPalette());

    const auto image = screenshot_image::Prepare(bits, screenshotMode);
    if (!image)
        return nullptr;
    const auto scrwidth = image->width;
    const auto scrheight = image->height;

    BITMAPINFOHEADER bi{};
    bi.biSize = sizeof(BITMAPINFOHEADER);
    bi.biWidth = scrwidth;
    bi.biHeight = scrheight;
    bi.biPlanes = 1;
    bi.biBitCount = 32;
    bi.biCompression = BI_RGB;
    bi.biSizeImage = bi.biWidth * bi.biHeight * 4;

    HGLOBAL hDIB = ::GlobalAlloc(GMEM_MOVEABLE, sizeof(BITMAPINFOHEADER) + bi.biSizeImage);
    if (hDIB == nullptr)
    {
        return nullptr;
    }

    LPBYTE p = (LPBYTE) ::GlobalLock(hDIB);
    if (p == nullptr)
    {
        ::GlobalFree(hDIB);
        return nullptr;
    }
    ::CopyMemory(p, &bi, sizeof(BITMAPINFOHEADER));
    p += sizeof(BITMAPINFOHEADER);
    if (!screenshot_image::CopyTopDown(*image, std::span{reinterpret_cast<std::byte*>(p), static_cast<size_t>(bi.biSizeImage)}))
    {
        ::GlobalUnlock(hDIB);
        ::GlobalFree(hDIB);
        return nullptr;
    }
    ::GlobalUnlock(hDIB);

    return hDIB;
}

// buffer size is 82 * 26 + 1 means 26 lines, 80 chars in every line plus CR/LF plus trailing zero
BOOL ScreenView_ScreenToText(std::span<uint8_t> buffer)
{
    if (buffer.size() < screen_text::OutputSize) return FALSE;
    // Get screenshot
    std::vector<uint32_t> bits(UKNC_SCREEN_WIDTH * UKNC_SCREEN_HEIGHT);
    Emulator_PrepareScreenToText(bits, video_palette::Get(video_palette::Mode::gray));

    // Prepare font, get current font data from PPU memory
    CMemoryController* pPpuMemCtl = g_pBoard->GetPPUMemoryController();
    uint8_t fontcur[11 * 16 * 14];
    uint16_t fontaddr = 014142 + 32 * 2;
    for (BYTE charidx = 0; charidx < 16 * 14; charidx++)
    {
        uint16_t charaddr = pPpuMemCtl->GetWordView(fontaddr + charidx * 2, false, false).value;
        for (int16_t y = 0; y < 11; y++)
        {
            uint16_t fontdata = pPpuMemCtl->GetWordView((charaddr + y) & ~1, false, false).value;
            if (((charaddr + y) & 1) == 1) fontdata >>= 8;
            fontcur[charidx * 11 + y] = (uint8_t)(fontdata & 0xff);
        }
    }
    // Prepare font, get standard font data from PPU memory
    uint8_t fontstd[11 * 16 * 14];
    uint16_t charstdaddr = 0120170;
    for (uint16_t idx = 0; idx < 16 * 14 * 11; idx++)
    {
        uint16_t fontdata = pPpuMemCtl->GetWordView(charstdaddr & ~1, false, false).value;
        if ((charstdaddr & 1) == 1) fontdata >>= 8;
        fontstd[idx] = (uint8_t)(fontdata & 0xff);
        charstdaddr++;
    }

    return screen_text::Convert(fontcur, fontstd, bits, buffer);
}


//////////////////////////////////////////////////////////////////////


LPCTSTR ScreenView_GetScreenshotModeName(int screenshotMode)
{
    const auto modes = screen_scale::Modes();
    if (screenshotMode < 0 || static_cast<size_t>(screenshotMode) >= modes.size())
        return nullptr;
    return modes[screenshotMode].description;
}

//////////////////////////////////////////////////////////////////////
