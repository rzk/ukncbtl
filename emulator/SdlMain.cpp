/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "EmbeddedRom.h"
#include "MediaSlots.h"
#include "PreviewSettings.h"
#include "PreviewSnapshot.h"
#include "ScreenshotImage.h"
#include "ScreenshotClipboard.h"
#include "ScreenshotFile.h"
#include "SdlAudio.h"
#include "SdlKeyboard.h"
#include "SdlFileDialog.h"
#include "SdlScreenRenderer.h"
#include "VideoDecoder.h"
#include "emubase/Board.h"
#include "emubase/Processor.h"
#include "util/ScreenScale.h"
#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cwchar>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
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
    preview_settings::Settings settings;
    std::optional<std::filesystem::path> settingsPath;
    std::string startupError;
    bool settingsDirty = false;
    if (!smokeTest)
    {
        const std::unique_ptr<char, decltype(&SDL_free)> directory(SDL_GetPrefPath("UKNCBTL", "SDL Preview"), SDL_free);
        if (directory)
        {
            const std::u8string utf8(reinterpret_cast<const char8_t*>(directory.get()));
            settingsPath = std::filesystem::path(utf8) / "settings.ini";
            const auto loaded = preview_settings::Load(*settingsPath);
            if (loaded) settings = *loaded;
            else { settingsPath.reset(); startupError = "Could not load preview settings. Changes will not be saved."; }
        }
        else startupError = "Could not locate preview settings. Changes will not be saved.";
    }
    const std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
        SDL_CreateWindow("UKNCBTL - SDL preview", settings.windowWidth, settings.windowHeight,
            SDL_WINDOW_RESIZABLE | (smokeTest ? SDL_WINDOW_HIDDEN : 0)), SDL_DestroyWindow);
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
    media_slots::Slots media(board);
    const auto fileDialog = std::make_shared<sdl_file_dialog::State>();
    std::string actionError = startupError;
    bool showActionError = !startupError.empty();
    std::array<std::uint8_t, 32768> rom{};
    std::ranges::copy(embedded_rom::Bytes(), rom.begin());
    board.LoadROM(rom);
    board.Reset();
    preview_snapshot::Session snapshots(board);
    std::unique_ptr<SDL_AudioStream, decltype(&SDL_DestroyAudioStream)> audioStream(nullptr, SDL_DestroyAudioStream);
    std::unique_ptr<sdl_audio::Output> audio;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO))
    {
        const SDL_AudioSpec spec{SDL_AUDIO_S16LE, sdl_audio::Channels, sdl_audio::SampleRate};
        audioStream.reset(SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr));
        if (audioStream && (!settings.sound || SDL_ResumeAudioStreamDevice(audioStream.get())))
            audio = std::make_unique<sdl_audio::Output>(board, *audioStream);
        else
            SDL_Log("Audio unavailable: %s", SDL_GetError());
    }
    else SDL_Log("Audio unavailable: %s", SDL_GetError());
    if (smokeTest && !audio) return 1;
    bool soundEnabled = audio != nullptr && settings.sound;
    bool audioActive = soundEnabled;
    constexpr auto palette = StandardPalette();
    std::vector<std::uint32_t> pixels(video::Width * video::Height);
    sdl_screen::Renderer screen(*renderer);
    sdl_keyboard::Keyboard keyboard;
    sdl_keyboard::Delivery keyDelivery;
    const auto releaseKeys = [&] {
        (void)keyboard.ReleaseAll();
        for (const auto event : keyDelivery.Reset()) board.KeyboardEvent(event.scan, event.pressed);
    };
    bool running = true;
    bool done = false;
    int screenshotMode = settings.screenshotMode;
    unsigned frames = 0;
    while (!done)
    {
        bool copyScreenshot = false;
        std::optional<std::filesystem::path> screenshotPath;
        const auto started = std::chrono::steady_clock::now();
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if ((event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) &&
                event.key.windowID == SDL_GetWindowID(window.get()))
            {
                const auto key = keyboard.Transition(event.key.scancode, event.type == SDL_EVENT_KEY_DOWN,
                    event.key.repeat, !running || ImGui::GetIO().WantCaptureKeyboard,
                    (board.GetKeyboardRegister() & KEYB_LAT) != 0);
                if (key) keyDelivery.Push(*key);
            }
            if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST && event.window.windowID == SDL_GetWindowID(window.get()))
                releaseKeys();
            if (event.type == SDL_EVENT_QUIT ||
                (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(window.get())))
                done = true;
        }
        if (done) break;
        if (const auto result = fileDialog->Take())
        {
            if (result->status == sdl_file_dialog::Status::failed)
            {
                actionError = "Could not open the file dialog.";
                showActionError = true;
            }
            else if (result->status == sdl_file_dialog::Status::selected)
            {
                const std::u8string path(reinterpret_cast<const char8_t*>(result->path.data()), result->path.size());
                if (result->purpose == sdl_file_dialog::Purpose::screenshot)
                {
                    const auto format = result->filter == 1 ? screenshot_format::Format::bmp : screenshot_format::Format::png;
                    screenshotPath = screenshot_file::WithDefaultExtension(std::filesystem::path(path), format);
                }
                else if (result->purpose == sdl_file_dialog::Purpose::snapshotLoad)
                {
                    if (!snapshots.Load(std::filesystem::path(path), [&] {
                        releaseKeys();
                        running = false;
                        if (audio)
                        {
                            const bool paused = SDL_PauseAudioStreamDevice(audioStream.get());
                            const bool cleared = audio->Reset();
                            if (!paused || !cleared)
                            {
                                SDL_Log("Audio pause failed: %s", SDL_GetError());
                                audio.reset(); audioStream.reset(); soundEnabled = false;
                            }
                            audioActive = false;
                        }
                    }))
                    {
                        actionError = "Could not load the state image.";
                        showActionError = true;
                    }
                }
                else if (result->purpose == sdl_file_dialog::Purpose::snapshotSave)
                {
                    const auto target = preview_snapshot::WithDefaultExtension(std::filesystem::path(path));
                    if (media.Conflicts(target))
                    {
                        actionError = "The selected file is an attached disk image. Choose another file.";
                        showActionError = true;
                    }
                    else if (!snapshots.Save(target))
                    {
                        actionError = "Could not save the state image.";
                        showActionError = true;
                    }
                }
                else if (!media.Attach(result->kind, result->slot, std::filesystem::path(path)))
                {
                    actionError = "Could not attach the disk image. Check the file and the selected drive.";
                    showActionError = true;
                }
            }
        }
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        if (ImGui::GetIO().WantCaptureKeyboard) releaseKeys();
        float menuHeight = 0;
        if (ImGui::BeginMainMenuBar())
        {
            menuHeight = ImGui::GetWindowHeight();
            if (ImGui::BeginMenu("Emulator"))
            {
                if (ImGui::MenuItem(running ? "Pause" : "Run")) { running = !running; if (!running) releaseKeys(); }
                if (ImGui::MenuItem("Reset"))
                {
                    releaseKeys();
                    board.Reset();
                    snapshots.ResetTracking();
                    if (audio && !audio->Reset())
                    {
                        SDL_Log("Audio reset failed: %s", SDL_GetError());
                        audio.reset(); audioStream.reset(); soundEnabled = audioActive = false;
                    }
                }
                if (ImGui::MenuItem("Load state...", nullptr, false, !fileDialog->Pending()) &&
                    !sdl_file_dialog::OpenSnapshot(fileDialog, *window))
                {
                    actionError = "Could not open the file dialog.";
                    showActionError = true;
                }
                if (ImGui::MenuItem("Save state...", nullptr, false, !fileDialog->Pending()) &&
                    !sdl_file_dialog::SaveSnapshot(fileDialog, *window))
                {
                    actionError = "Could not open the file dialog.";
                    showActionError = true;
                }
                if (ImGui::MenuItem("Sound", nullptr, &soundEnabled, audio != nullptr))
                {
                    settings.sound = soundEnabled;
                    settingsDirty = true;
                }
                if (ImGui::MenuItem("Quit")) done = true;
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Screenshot"))
            {
                if (ImGui::MenuItem("Copy to clipboard")) copyScreenshot = true;
                if (ImGui::MenuItem("Save as...", nullptr, false, !fileDialog->Pending()))
                {
                    if (!sdl_file_dialog::SaveScreenshot(fileDialog, *window))
                    {
                        actionError = "Could not open the file dialog.";
                        showActionError = true;
                    }
                }
                if (ImGui::BeginMenu("Size"))
                {
                    const auto modes = screen_scale::Modes();
                    for (std::size_t index = 0; index < modes.size(); ++index)
                    {
                        std::string label;
                        for (const wchar_t* ch = modes[index].description; *ch; ++ch) label.push_back(static_cast<char>(*ch));
                        if (ImGui::MenuItem(label.c_str(), nullptr, screenshotMode == static_cast<int>(index)))
                        {
                            screenshotMode = static_cast<int>(index);
                            settings.screenshotMode = screenshotMode;
                            settingsDirty = true;
                        }
                    }
                    ImGui::EndMenu();
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Disks"))
            {
                const auto driveMenu = [&](media_slots::Kind kind, int slot, const std::string& label) {
                    if (!ImGui::BeginMenu(label.c_str())) return;
                    if (media.Attached(kind, slot))
                    {
                        const auto path = media.Path(kind, slot)->u8string();
                        ImGui::TextUnformatted(reinterpret_cast<const char*>(path.c_str()));
                        if (media.ReadOnly(kind, slot)) ImGui::TextDisabled("Read-only");
                        if (ImGui::MenuItem("Eject") && !media.Eject(kind, slot))
                        {
                            actionError = "Could not eject the disk image.";
                            showActionError = true;
                        }
                    }
                    else if (ImGui::MenuItem("Attach...", nullptr, false, !fileDialog->Pending()))
                    {
                        if (!sdl_file_dialog::Open(fileDialog, *window, kind, slot))
                        {
                            actionError = "Could not open the file dialog.";
                            showActionError = true;
                        }
                    }
                    ImGui::EndMenu();
                };
                for (int slot = 0; slot < 4; ++slot)
                    driveMenu(media_slots::Kind::floppy, slot, "Floppy " + std::to_string(slot + 1));
                ImGui::Separator();
                for (int slot = 0; slot < 2; ++slot)
                    driveMenu(media_slots::Kind::hardDisk, slot, "Hard disk " + std::to_string(slot + 1));
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }
        if (showActionError) { ImGui::OpenPopup("Error"); showActionError = false; }
        if (ImGui::BeginPopupModal("Error", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextWrapped("%s", actionError.c_str());
            if (ImGui::Button("OK")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        const bool shouldPlay = running && soundEnabled && audio != nullptr;
        if (audio && shouldPlay != audioActive)
        {
            const bool paused = SDL_PauseAudioStreamDevice(audioStream.get());
            const bool cleared = audio->Reset();
            const bool resumed = !shouldPlay || SDL_ResumeAudioStreamDevice(audioStream.get());
            if (!paused || !cleared || !resumed)
            {
                SDL_Log("Audio device failed: %s", SDL_GetError());
                audio.reset(); audioStream.reset(); soundEnabled = false;
            }
            audioActive = audio && shouldPlay;
        }
        if (running)
        {
            if (const auto key = keyDelivery.Next()) board.KeyboardEvent(key->scan, key->pressed);
            if (!board.SystemFrame()) { SDL_Log("Emulator frame failed"); return 1; }
            snapshots.FrameCompleted();
            if (audio && audioActive && !audio->SubmitFrame())
            {
                SDL_Log("Audio submission failed: %s", SDL_GetError());
                audio.reset(); audioStream.reset(); soundEnabled = audioActive = false;
            }
        }
        if (!video::RenderRgb(board, pixels, palette)) return 1;
        const auto image = screenshot_image::Prepare(pixels, 1);
        if (!image) return 1;
        if (copyScreenshot || screenshotPath)
        {
            const auto capture = screenshotMode == 1 ? image : screenshot_image::Prepare(pixels, screenshotMode);
            if (!capture)
            {
                actionError = "Could not prepare the screenshot image.";
                showActionError = true;
            }
            else
            {
                if (copyScreenshot && !screenshot_clipboard::SetImage(*capture))
                {
                    actionError = "Could not copy the screenshot to the clipboard.";
                    showActionError = true;
                }
                if (screenshotPath && media.Conflicts(*screenshotPath))
                {
                    if (showActionError) actionError += "\n";
                    else actionError.clear();
                    actionError += "The selected file is an attached disk image. Choose another file.";
                    showActionError = true;
                }
                else if (screenshotPath && !screenshot_file::Save(*screenshotPath, *capture))
                {
                    if (showActionError) actionError += "\n";
                    else actionError.clear();
                    actionError += "Could not save the screenshot image.";
                    showActionError = true;
                }
            }
        }
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
    if (settingsPath)
    {
        int width = 0, height = 0;
        const auto flags = SDL_GetWindowFlags(window.get());
        if (!(flags & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_MAXIMIZED | SDL_WINDOW_FULLSCREEN)) &&
            SDL_GetWindowSize(window.get(), &width, &height) && preview_settings::ValidWindowSize(width, height) &&
            (width != settings.windowWidth || height != settings.windowHeight))
        {
            settings.windowWidth = width;
            settings.windowHeight = height;
            settingsDirty = true;
        }
        if (settingsDirty && !preview_settings::Save(*settingsPath, settings))
        {
            SDL_Log("Could not save preview settings.");
            (void)SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Settings", "Could not save preview settings.", window.get());
        }
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
