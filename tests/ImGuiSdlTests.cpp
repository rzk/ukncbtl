/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "SdlScreenRenderer.h"
#include <SDL3/SDL.h>
#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>
#include <memory>

namespace
{
class ImGuiSdlTest : public testing::Test
{
protected:
    std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window{nullptr, SDL_DestroyWindow};
    std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> renderer{nullptr, SDL_DestroyRenderer};
    bool platformReady = false;
    bool rendererReady = false;
    void SetUp() override
    {
        ASSERT_TRUE(SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "dummy", SDL_HINT_OVERRIDE));
        ASSERT_TRUE(SDL_Init(SDL_INIT_VIDEO)) << SDL_GetError();
        window.reset(SDL_CreateWindow("ImGui test", 64, 64, SDL_WINDOW_HIDDEN));
        ASSERT_TRUE(window) << SDL_GetError();
        renderer.reset(SDL_CreateRenderer(window.get(), "software"));
        ASSERT_TRUE(renderer) << SDL_GetError();
        IMGUI_CHECKVERSION();
        ASSERT_NE(ImGui::CreateContext(), nullptr);
        ImGui::GetIO().IniFilename = nullptr;
        platformReady = ImGui_ImplSDL3_InitForSDLRenderer(window.get(), renderer.get());
        ASSERT_TRUE(platformReady);
        rendererReady = ImGui_ImplSDLRenderer3_Init(renderer.get());
        ASSERT_TRUE(rendererReady);
    }
    void TearDown() override
    {
        if (rendererReady) ImGui_ImplSDLRenderer3_Shutdown();
        if (platformReady) ImGui_ImplSDL3_Shutdown();
        if (ImGui::GetCurrentContext()) ImGui::DestroyContext();
        renderer.reset();
        window.reset();
        SDL_Quit();
    }
};

TEST_F(ImGuiSdlTest, DrawsUiOverEmulatorFrameAndHandlesKeyboardEvents)
{
    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.windowID = SDL_GetWindowID(window.get());
    event.key.scancode = SDL_SCANCODE_A;
    event.key.key = SDLK_A;
    event.key.down = true;
    ASSERT_TRUE(ImGui_ImplSDL3_ProcessEvent(&event));

    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    EXPECT_TRUE(ImGui::IsKeyDown(ImGuiKey_A));
    ImGui::GetForegroundDrawList()->AddRectFilled(ImVec2(8, 8), ImVec2(24, 24), IM_COL32(255, 0, 0, 255));
    ImGui::Render();
    ASSERT_GT(ImGui::GetDrawData()->TotalVtxCount, 0);

    ASSERT_TRUE(SDL_SetRenderDrawColor(renderer.get(), 0, 0, 0, 255));
    ASSERT_TRUE(SDL_RenderClear(renderer.get()));
    sdl_screen::Renderer screen(*renderer);
    ASSERT_TRUE(screen.Draw({1, 1, {0x000000ff}}, SDL_FRect{0, 0, 64, 64}));
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer.get());
    const std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> output(
        SDL_RenderReadPixels(renderer.get(), nullptr), SDL_DestroySurface);
    ASSERT_TRUE(output) << SDL_GetError();
    Uint8 r = 0, g = 0, b = 0, a = 0;
    ASSERT_TRUE(SDL_ReadSurfacePixel(output.get(), 12, 12, &r, &g, &b, &a));
    EXPECT_EQ(r, 255); EXPECT_EQ(g, 0); EXPECT_EQ(b, 0);
    ASSERT_TRUE(SDL_ReadSurfacePixel(output.get(), 40, 40, &r, &g, &b, &a));
    EXPECT_EQ(r, 0); EXPECT_EQ(g, 0); EXPECT_EQ(b, 255);
}
}
