/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "EmbeddedRom.h"
#include "ScreenshotImage.h"
#include "SdlScreenRenderer.h"
#include "VideoDecoder.h"
#include "emubase/Board.h"
#include "emubase/Processor.h"
#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cwchar>
#include <memory>
#include <string_view>
#include <thread>
#include <vector>

// The portable core delegates diagnostics to its host.
void DebugLog(const wchar_t* message) { std::fwprintf(stderr, L"%ls", message); }

namespace
{
struct ProcessorRuntime
{
    ProcessorRuntime() { CProcessor::Init(); }
    ~ProcessorRuntime() { CProcessor::Done(); }
};
struct SdlRuntime { ~SdlRuntime() { SDL_Quit(); } };
struct ImGuiRuntime
{
    bool platform = false;
    bool renderer = false;
    ~ImGuiRuntime()
    {
        if (renderer) ImGui_ImplSDLRenderer3_Shutdown();
        if (platform) ImGui_ImplSDL3_Shutdown();
        if (ImGui::GetCurrentContext()) ImGui::DestroyContext();
    }
};

constexpr std::array<std::uint32_t, 128> StandardPalette()
{
    std::array<std::uint32_t, 128> colors{};
    for (int brightness = 0; brightness < 8; ++brightness)
        for (int color = 0; color < 16; ++color)
        {
            const int level = (color & 8) ? 255 : 128;
            const int r = (color & 4) ? level - ((brightness & 4) ? 32 : 0) : 0;
            const int g = (color & 2) ? level - ((brightness & 2) ? 32 : 0) : 0;
            const int b = (color & 1) ? level - ((brightness & 1) ? 32 : 0) : 0;
            colors[brightness * 16 + color] = static_cast<std::uint32_t>((r << 16) | (g << 8) | b);
        }
    return colors;
}
static_assert(StandardPalette()[1] == 0x000080 && StandardPalette()[15] == 0xffffff &&
    StandardPalette()[31] == 0xffffdf && StandardPalette()[127] == 0xdfdfdf);

int Run(bool smokeTest)
{
    if (!SDL_Init(SDL_INIT_VIDEO)) { SDL_Log("SDL initialization failed: %s", SDL_GetError()); return 1; }
    SdlRuntime sdl;
    const std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
        SDL_CreateWindow("UKNCBTL - SDL preview", 960, 720, SDL_WINDOW_RESIZABLE | (smokeTest ? SDL_WINDOW_HIDDEN : 0)), SDL_DestroyWindow);
    if (!window) { SDL_Log("Window creation failed: %s", SDL_GetError()); return 1; }
    const std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> renderer(
        SDL_CreateRenderer(window.get(), nullptr), SDL_DestroyRenderer);
    if (!renderer) { SDL_Log("Renderer creation failed: %s", SDL_GetError()); return 1; }

    ImGuiRuntime gui;
    IMGUI_CHECKVERSION();
    if (!ImGui::CreateContext()) return 1;
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::StyleColorsDark();
    gui.platform = ImGui_ImplSDL3_InitForSDLRenderer(window.get(), renderer.get());
    if (!gui.platform) { SDL_Log("ImGui SDL initialization failed"); return 1; }
    gui.renderer = ImGui_ImplSDLRenderer3_Init(renderer.get());
    if (!gui.renderer) { SDL_Log("ImGui renderer initialization failed"); return 1; }

    ProcessorRuntime processor;
    CMotherboard board;
    std::array<std::uint8_t, 32768> rom{};
    std::ranges::copy(embedded_rom::Bytes(), rom.begin());
    board.LoadROM(rom);
    board.Reset();
    constexpr auto palette = StandardPalette();
    std::vector<std::uint32_t> pixels(video::Width * video::Height);
    sdl_screen::Renderer screen(*renderer);
    bool running = true;
    bool done = false;
    unsigned frames = 0;
    while (!done)
    {
        const auto started = std::chrono::steady_clock::now();
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT ||
                (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(window.get())))
                done = true;
        }
        if (done) break;
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        float menuHeight = 0;
        if (ImGui::BeginMainMenuBar())
        {
            menuHeight = ImGui::GetWindowHeight();
            if (ImGui::BeginMenu("Emulator"))
            {
                if (ImGui::MenuItem(running ? "Pause" : "Run")) running = !running;
                if (ImGui::MenuItem("Reset")) board.Reset();
                if (ImGui::MenuItem("Quit")) done = true;
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }
        if (running && !board.SystemFrame()) { SDL_Log("Emulator frame failed"); return 1; }
        if (!video::RenderRgb(board, pixels, palette)) return 1;
        const auto image = screenshot_image::Prepare(pixels, 1);
        if (!image) return 1;
        ImGui::Render();
        int width = 0, height = 0;
        if (!SDL_GetRenderOutputSize(renderer.get(), &width, &height)) return 1;
        const float top = menuHeight * ImGui::GetIO().DisplayFramebufferScale.y;
        const float availableHeight = std::max(0.0f, static_cast<float>(height) - top);
        const float drawWidth = std::min(static_cast<float>(width), availableHeight * 4.0f / 3.0f);
        const float drawHeight = drawWidth * 3.0f / 4.0f;
        const SDL_FRect destination{(width - drawWidth) / 2.0f, top + (availableHeight - drawHeight) / 2.0f, drawWidth, drawHeight};
        if (!SDL_SetRenderDrawColor(renderer.get(), 0, 0, 0, 255) || !SDL_RenderClear(renderer.get())) return 1;
        if (drawWidth > 0 && drawHeight > 0 && !screen.Draw(*image, destination)) { SDL_Log("Screen draw failed: %s", SDL_GetError()); return 1; }
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer.get());
        if (!SDL_RenderPresent(renderer.get())) return 1;
        if (smokeTest && ++frames == 3) break;
        std::this_thread::sleep_until(started + std::chrono::milliseconds(20));
    }
    return 0;
}
}

int main(int argc, char** argv)
{
    if (argc > 2 || (argc == 2 && std::string_view(argv[1]) != "--smoke-test"))
    {
        std::fprintf(stderr, "Usage: ukncbtl-sdl [--smoke-test]\n");
        return 2;
    }
    return Run(argc == 2);
}
