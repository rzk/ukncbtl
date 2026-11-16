/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "MediaSlots.h"
#include "AutoBoot.h"
#include "DiskImageFile.h"
#include "MouseInput.h"
#include "FramePacing.h"
#include "PreviewRoms.h"
#include "PreviewPrinter.h"
#include "PreviewOptions.h"
#include "PreviewDebugger.h"
#include "PreviewSettings.h"
#include "PreviewSnapshot.h"
#include "PreviewTape.h"
#include "ScreenshotImage.h"
#include "ScreenshotClipboard.h"
#include "ScreenshotFile.h"
#include "ScreenTextCapture.h"
#include "SdlAudio.h"
#include "SdlDisplay.h"
#include "SdlKeyboard.h"
#include "SdlFileDialog.h"
#include "SdlScreenRenderer.h"
#include "VideoDecoder.h"
#include "VideoPalette.h"
#include "VirtualKeyboard.h"
#include "emubase/Board.h"
#include "emubase/Processor.h"
#include "util/ScreenScale.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
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
#include <utility>
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

int Run(const preview_options::Options& options)
{
    const bool smokeTest = options.smokeTest;
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
    else settings.keyboard = true; // Exercise the keyboard window in the headless smoke test.
    const std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
        SDL_CreateWindow("UKNCBTL - SDL preview", settings.windowWidth, settings.windowHeight,
            SDL_WINDOW_RESIZABLE | (smokeTest ? SDL_WINDOW_HIDDEN : 0)), SDL_DestroyWindow);
    if (!window) { SDL_Log("Window creation failed: %s", SDL_GetError()); return 1; }
    if (options.fullscreen.value_or(false) && !SDL_SetWindowFullscreen(window.get(), true))
    { SDL_Log("Fullscreen request failed: %s", SDL_GetError()); return 1; }
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
    preview_roms::Roms roms(board);
    if (options.rom && !roms.LoadSystem(*options.rom))
    { SDL_Log("Could not load the requested system ROM."); return 1; }
    for (int slot = 0; slot < 2; ++slot)
        if (options.cartridges[slot] && !roms.Insert(slot, *options.cartridges[slot]))
        { SDL_Log("Could not load requested cartridge %d.", slot + 1); return 1; }
    for (int slot = 0; slot < 4; ++slot)
        if (options.floppies[slot] && !media.Attach(media_slots::Kind::floppy, slot, *options.floppies[slot]))
        { SDL_Log("Could not attach requested floppy %d.", slot); return 1; }
    for (int slot = 0; slot < 2; ++slot)
        if (options.hardDisks[slot] && !media.Attach(media_slots::Kind::hardDisk, slot, *options.hardDisks[slot]))
        { SDL_Log("Could not attach requested hard disk %d; check its ROM cartridge and image.", slot + 1); return 1; }
    preview_printer::Session printer(board);
    preview_tape::Session tape(board);
    preview_debugger::Session debugger(board);
    const auto fileDialog = std::make_shared<sdl_file_dialog::State>();
    std::string actionError = startupError;
    bool showActionError = !startupError.empty();
    board.Reset();
    board.SetSoundAY(settings.soundAY);
    board.SetSoundCovox(settings.soundCovox);
    board.SetMouse(settings.mouse);
    preview_snapshot::Session snapshots(board);
    std::unique_ptr<SDL_AudioStream, decltype(&SDL_DestroyAudioStream)> audioStream(nullptr, SDL_DestroyAudioStream);
    std::unique_ptr<sdl_audio::Output> audio;
    const bool initialSound = options.sound.value_or(settings.sound);
    if (SDL_InitSubSystem(SDL_INIT_AUDIO))
    {
        const SDL_AudioSpec spec{SDL_AUDIO_S16LE, sdl_audio::Channels, sdl_audio::SampleRate};
        audioStream.reset(SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr));
        if (audioStream)
        {
            audio = std::make_unique<sdl_audio::Output>(board, *audioStream);
            if (!audio->Configure(settings.speed, settings.volume) ||
                (initialSound && !SDL_ResumeAudioStreamDevice(audioStream.get())))
            {
                SDL_Log("Audio unavailable: %s", SDL_GetError());
                audio.reset(); audioStream.reset();
            }
        }
        else SDL_Log("Audio unavailable: %s", SDL_GetError());
    }
    else SDL_Log("Audio unavailable: %s", SDL_GetError());
    if (smokeTest && !audio) return 1;
    bool soundEnabled = audio != nullptr && initialSound;
    bool audioActive = soundEnabled;
    std::vector<std::uint32_t> pixels(video::Width * video::Height);
    sdl_screen::Renderer screen(*renderer);
    sdl_keyboard::Keyboard keyboard;
    sdl_keyboard::Merge keyMerge;
    sdl_keyboard::Delivery keyDelivery;
    virtual_keyboard::State virtualKeys;
    auto_boot::Sequence autoBoot;
    const auto queueKey = [&](sdl_keyboard::Source source, sdl_keyboard::Event event) {
        if (const auto merged = keyMerge.Transition(source, event)) keyDelivery.Push(*merged);
    };
    const auto releasePhysicalKeys = [&] {
        for (const auto event : keyboard.ReleaseAll()) queueKey(sdl_keyboard::Source::physical, event);
    };
    const auto releaseVirtualKeys = [&] {
        for (std::uint8_t scan = 1; scan < 128; ++scan)
            if (virtualKeys.Down(scan)) queueKey(sdl_keyboard::Source::virtualKey, {scan, false});
        virtualKeys.Reset();
        autoBoot.Cancel();
    };
    mouse_input::Tracker mouse;
    const auto releaseMouse = [&] {
        mouse.Reset();
        board.MouseMove(0, 0, false, false);
    };
    const auto releaseKeys = [&] {
        (void)keyboard.ReleaseAll();
        virtualKeys.Reset();
        keyMerge.Reset();
        for (const auto event : keyDelivery.Reset()) board.KeyboardEvent(event.scan, event.pressed);
    };
    const auto resetMachine = [&] {
        releaseKeys();
        releaseMouse();
        debugger.Stop();
        board.Reset();
        board.SetMouse(settings.mouse);
        printer.Synchronize();
        snapshots.ResetTracking();
        if (audio && !audio->Reset())
        {
            SDL_Log("Audio reset failed: %s", SDL_GetError());
            audio.reset(); audioStream.reset(); soundEnabled = audioActive = false;
        }
    };
    const auto conflicts = [&](const std::filesystem::path& path) {
        return media.Conflicts(path) || roms.Conflicts(path) || printer.Conflicts(path) || tape.Conflicts(path);
    };
    bool running = smokeTest || options.boot != 0 || options.run.value_or(settings.autostart);
    if (options.boot) (void)autoBoot.Start(options.boot);
    frame_pacing::Scheduler scheduler(frame_pacing::Clock::now());
    const auto pauseMachine = [&] {
        running = false;
        releaseKeys(); releaseMouse(); debugger.Stop();
        if (!printer.Flush()) { actionError = "Printer output failed and was stopped."; showActionError = true; }
    };
    const auto startMachine = [&](frame_pacing::Clock::time_point now) {
        debugger.PrepareRun(); running = true; scheduler.Restart(now);
    };
    bool done = false;
    bool showDebugger = smokeTest;
    preview_debugger::Processor debugProcessor = preview_debugger::Processor::cpu;
    std::array<char, 16> breakpointAddress{};
    std::array<char, 16> memoryAddress{'0', '0', '0', '0', '0', '0', 0};
    std::string debuggerError;
    int screenshotMode = settings.screenshotMode;
    int displayMode = settings.displayMode;
    int paletteMode = settings.paletteMode;
    unsigned frames = 0;
    while (!done)
    {
        bool copyScreenshot = false;
        bool copyScreenText = false;
        bool fullscreenShortcut = false;
        bool toggleFullscreen = false;
        bool speedChanged = false;
        bool volumeChanged = false;
        std::optional<std::filesystem::path> screenshotPath;
        std::vector<SDL_Event> mouseEvents;
        bool focusLost = false;
        const auto started = std::chrono::steady_clock::now();
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_KEY_DOWN && event.key.windowID == SDL_GetWindowID(window.get()) &&
                event.key.scancode == SDL_SCANCODE_F10 && !event.key.repeat &&
                !(event.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_SHIFT | SDL_KMOD_ALT | SDL_KMOD_GUI)))
                fullscreenShortcut = true;
            if ((event.type == SDL_EVENT_MOUSE_MOTION && event.motion.windowID == SDL_GetWindowID(window.get())) ||
                ((event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP) &&
                    event.button.windowID == SDL_GetWindowID(window.get())))
                mouseEvents.push_back(event);
            if ((event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) &&
                event.key.windowID == SDL_GetWindowID(window.get()))
            {
                const auto key = keyboard.Transition(event.key.scancode, event.type == SDL_EVENT_KEY_DOWN,
                    event.key.repeat, !running || ImGui::GetIO().WantCaptureKeyboard,
                    (board.GetKeyboardRegister() & KEYB_LAT) != 0);
                if (key) queueKey(sdl_keyboard::Source::physical, *key);
            }
            if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST && event.window.windowID == SDL_GetWindowID(window.get()))
            { releaseKeys(); focusLost = true; }
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
                if (result->purpose == sdl_file_dialog::Purpose::tapeOpen ||
                    result->purpose == sdl_file_dialog::Purpose::tapeCreate)
                {
                    auto target = std::filesystem::path(path);
                    const bool creating = result->purpose == sdl_file_dialog::Purpose::tapeCreate;
                    if (creating && !target.has_extension()) target += ".wav";
                    if ((creating && conflicts(target)) || (!creating && (printer.Conflicts(target) || tape.Conflicts(target))))
                    { actionError = "The selected file is used by the emulator. Choose another file."; showActionError = true; }
                    else if (!(creating ? tape.Create(target) : tape.Open(target)))
                    { actionError = creating ? "Could not create the WAV file." : "Could not open the WAV file or its format is unsupported."; showActionError = true; }
                }
                else if (result->purpose == sdl_file_dialog::Purpose::createDisk)
                {
                    auto target = std::filesystem::path(path);
                    if (!target.has_extension()) target += ".dsk";
                    if (conflicts(target))
                    { actionError = "The selected file is used by the emulator. Choose another file."; showActionError = true; }
                    else if (!disk_image::CreatePortable(target, result->slot))
                    { actionError = "Could not create the disk image. The file may be incomplete."; showActionError = true; }
                }
                else if (result->purpose == sdl_file_dialog::Purpose::printer)
                {
                    const auto target = std::filesystem::path(path);
                    if (conflicts(target))
                    { actionError = "The selected file is used by the emulator. Choose another file."; showActionError = true; }
                    else if (!printer.Start(target))
                    { actionError = "Could not open the printer output file."; showActionError = true; }
                }
                else if (result->purpose != sdl_file_dialog::Purpose::screenshot &&
                    result->purpose != sdl_file_dialog::Purpose::snapshotSave &&
                    (printer.Conflicts(std::filesystem::path(path)) || tape.Conflicts(std::filesystem::path(path))))
                { actionError = "The selected file is used for printer or tape output. Choose another file."; showActionError = true; }
                else if (result->purpose == sdl_file_dialog::Purpose::screenshot)
                {
                    const auto format = result->filter == 1 ? screenshot_format::Format::bmp : screenshot_format::Format::png;
                    screenshotPath = screenshot_file::WithDefaultExtension(std::filesystem::path(path), format);
                }
                else if (result->purpose == sdl_file_dialog::Purpose::snapshotLoad)
                {
                    if (!snapshots.Load(std::filesystem::path(path), [&] {
                        releaseKeys();
                        releaseMouse();
                        debugger.Stop();
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
                    else { roms.MarkRestoredFromSnapshot(); printer.Synchronize(); }
                }
                else if (result->purpose == sdl_file_dialog::Purpose::snapshotSave)
                {
                    const auto target = preview_snapshot::WithDefaultExtension(std::filesystem::path(path));
                    if (conflicts(target))
                    {
                        actionError = "The selected file is used by the emulator. Choose another file.";
                        showActionError = true;
                    }
                    else if (!snapshots.Save(target))
                    {
                        actionError = "Could not save the state image.";
                        showActionError = true;
                    }
                }
                else if (result->purpose == sdl_file_dialog::Purpose::systemRom)
                {
                    if (roms.LoadSystem(std::filesystem::path(path))) { resetMachine(); scheduler.Restart(started); }
                    else { actionError = "Could not load the system ROM image."; showActionError = true; }
                }
                else if (result->purpose == sdl_file_dialog::Purpose::cartridge)
                {
                    if (!roms.Insert(result->slot, std::filesystem::path(path)))
                    {
                        actionError = "Could not load the ROM cartridge image.";
                        showActionError = true;
                    }
                }
                else if (result->kind == media_slots::Kind::hardDisk && !roms.Inserted(result->slot))
                {
                    actionError = "Insert the matching hard-disk ROM cartridge first.";
                    showActionError = true;
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
        if (fullscreenShortcut && !ImGui::GetIO().WantCaptureKeyboard && !fileDialog->Pending()) toggleFullscreen = true;
        if (ImGui::GetIO().WantCaptureKeyboard) releasePhysicalKeys();
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
            if (const auto event = virtualKeys.ReleasePointer())
                queueKey(sdl_keyboard::Source::virtualKey, {event->scan, event->pressed});
        const auto selectScaleMode = [&](int& mode, int& setting) {
            const auto modes = screen_scale::Modes();
            for (std::size_t index = 0; index < modes.size(); ++index)
            {
                std::string label;
                for (const wchar_t* ch = modes[index].description; *ch; ++ch) label.push_back(static_cast<char>(*ch));
                if (ImGui::MenuItem(label.c_str(), nullptr, mode == static_cast<int>(index)))
                {
                    mode = static_cast<int>(index);
                    setting = mode;
                    settingsDirty = true;
                }
            }
        };
        float menuHeight = 0;
        if (ImGui::BeginMainMenuBar())
        {
            menuHeight = ImGui::GetWindowHeight();
            if (ImGui::BeginMenu("Emulator"))
            {
                if (ImGui::MenuItem(running ? "Pause" : "Run"))
                {
                    if (running) pauseMachine();
                    else startMachine(started);
                }
                if (ImGui::MenuItem("Reset")) { resetMachine(); scheduler.Restart(started); }
                if (ImGui::MenuItem("Start automatically", nullptr, &settings.autostart)) settingsDirty = true;
                if (ImGui::BeginMenu("Reset and boot"))
                {
                    for (int selection = 1; selection <= 7; ++selection)
                    {
                        const auto label = "Boot menu " + std::to_string(selection);
                        if (ImGui::MenuItem(label.c_str()))
                        {
                            resetMachine();
                            (void)autoBoot.Start(selection);
                            startMachine(started);
                        }
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::MenuItem("Mouse", nullptr, &settings.mouse))
                {
                    releaseMouse();
                    board.SetMouse(settings.mouse);
                    settingsDirty = true;
                }
                if (ImGui::BeginMenu("System ROM"))
                {
                    if (const auto* path = roms.SystemPath())
                    {
                        const auto text = path->u8string();
                        ImGui::TextUnformatted(reinterpret_cast<const char*>(text.c_str()));
                    }
                    else ImGui::TextDisabled(roms.Source() == preview_roms::SystemSource::embedded ? "Embedded ROM" : "Restored from state");
                    if (ImGui::MenuItem("Load ROM and reset...", nullptr, false, !fileDialog->Pending()) &&
                        !sdl_file_dialog::OpenRom(fileDialog, *window, sdl_file_dialog::Purpose::systemRom))
                    { actionError = "Could not open the file dialog."; showActionError = true; }
                    if (ImGui::MenuItem("Use embedded ROM and reset", nullptr, false,
                        roms.Source() != preview_roms::SystemSource::embedded))
                    { roms.UseEmbedded(); resetMachine(); scheduler.Restart(started); }
                    ImGui::EndMenu();
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
                if (ImGui::BeginMenu("Speed"))
                {
                    constexpr std::array choices{
                        std::pair{"10%", std::uint16_t{0x7ffd}}, std::pair{"25%", std::uint16_t{0x7ffe}},
                        std::pair{"50%", std::uint16_t{0x7fff}}, std::pair{"100%", std::uint16_t{1}},
                        std::pair{"200%", std::uint16_t{2}}, std::pair{"400%", std::uint16_t{3}},
                        std::pair{"Maximum", std::uint16_t{0}}};
                    for (const auto& [label, speed] : choices)
                        if (ImGui::MenuItem(label, nullptr, settings.speed == speed))
                        {
                            settings.speed = speed;
                            settingsDirty = speedChanged = true;
                            scheduler.Restart(started);
                        }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Audio"))
                {
                    if (ImGui::MenuItem("AY sound", nullptr, &settings.soundAY))
                    { board.SetSoundAY(settings.soundAY); settingsDirty = true; }
                    if (ImGui::MenuItem("Covox sound", nullptr, &settings.soundCovox))
                    { board.SetSoundCovox(settings.soundCovox); settingsDirty = true; }
                    if (ImGui::SliderInt("Volume", &settings.volume, 0, 100, "%d%%"))
                        settingsDirty = volumeChanged = true;
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Printer"))
                {
                    if (printer.Active())
                    {
                        const auto path = printer.Path().u8string();
                        ImGui::TextUnformatted(reinterpret_cast<const char*>(path.c_str()));
                        if (settings.soundCovox) ImGui::TextDisabled("Output paused while Covox is enabled");
                        if (ImGui::MenuItem("Stop output") && !printer.Stop())
                        { actionError = "Could not finish the printer output file."; showActionError = true; }
                    }
                    else if (ImGui::MenuItem("Start output...", nullptr, false, !fileDialog->Pending()) &&
                        !sdl_file_dialog::SavePrinter(fileDialog, *window))
                    { actionError = "Could not open the file dialog."; showActionError = true; }
                    ImGui::EndMenu();
                }
                if (ImGui::MenuItem("Quit")) done = true;
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Display"))
            {
                if (ImGui::MenuItem("On-screen keyboard", nullptr, &settings.keyboard))
                {
                    if (!settings.keyboard) releaseVirtualKeys();
                    settingsDirty = true;
                }
                if (ImGui::MenuItem("Fullscreen", "F10", (SDL_GetWindowFlags(window.get()) & SDL_WINDOW_FULLSCREEN) != 0))
                    toggleFullscreen = true;
                ImGui::Separator();
                if (ImGui::BeginMenu("Color"))
                {
                    constexpr std::array labels{"RGB", "GRB", "Grayscale"};
                    for (std::size_t index = 0; index < labels.size(); ++index)
                        if (ImGui::MenuItem(labels[index], nullptr, paletteMode == static_cast<int>(index)))
                        {
                            paletteMode = static_cast<int>(index);
                            settings.paletteMode = paletteMode;
                            settingsDirty = true;
                        }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Scaling"))
                {
                    selectScaleMode(displayMode, settings.displayMode);
                    ImGui::EndMenu();
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Debug"))
            {
                ImGui::MenuItem("Debugger", nullptr, &showDebugger);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Screenshot"))
            {
                if (ImGui::MenuItem("Copy to clipboard")) copyScreenshot = true;
                if (ImGui::MenuItem("Copy screen text")) copyScreenText = true;
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
                    selectScaleMode(screenshotMode, settings.screenshotMode);
                    ImGui::EndMenu();
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Cartridges"))
            {
                for (int slot = 0; slot < 2; ++slot)
                {
                    const auto label = "Cartridge " + std::to_string(slot + 1);
                    if (!ImGui::BeginMenu(label.c_str())) continue;
                    if (roms.Inserted(slot))
                    {
                        const auto text = roms.CartridgePath(slot)->u8string();
                        ImGui::TextUnformatted(reinterpret_cast<const char*>(text.c_str()));
                        if (ImGui::MenuItem("Eject")) (void)roms.Eject(slot);
                    }
                    else if (ImGui::MenuItem("Insert...", nullptr, false, !fileDialog->Pending()) &&
                        !sdl_file_dialog::OpenRom(fileDialog, *window, sdl_file_dialog::Purpose::cartridge, slot))
                    { actionError = "Could not open the file dialog."; showActionError = true; }
                    ImGui::EndMenu();
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Disks"))
            {
                if (ImGui::BeginMenu("Create floppy image"))
                {
                    for (const int tracks : {40, 80})
                    {
                        const auto label = std::to_string(tracks) + " tracks...";
                        if (ImGui::MenuItem(label.c_str(), nullptr, false, !fileDialog->Pending()) &&
                            !sdl_file_dialog::SaveDisk(fileDialog, *window, tracks))
                        { actionError = "Could not open the file dialog."; showActionError = true; }
                    }
                    ImGui::EndMenu();
                }
                ImGui::Separator();
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
                        if (kind == media_slots::Kind::hardDisk && !roms.Inserted(slot))
                        {
                            actionError = "Insert the matching hard-disk ROM cartridge first.";
                            showActionError = true;
                        }
                        else if (!sdl_file_dialog::Open(fileDialog, *window, kind, slot))
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
            if (ImGui::BeginMenu("Tape"))
            {
                if (tape.Inserted())
                {
                    const auto path = tape.Path().u8string();
                    ImGui::TextUnformatted(reinterpret_cast<const char*>(path.c_str()));
                    const auto position = tape::Centiseconds(tape.Position(), tape.Frequency());
                    const auto length = tape::Centiseconds(tape.Length(), tape.Frequency());
                    ImGui::Text("%llu:%02llu.%02llu / %llu:%02llu.%02llu  (%d Hz)",
                        static_cast<unsigned long long>(position / 6000), static_cast<unsigned long long>(position / 100 % 60),
                        static_cast<unsigned long long>(position % 100), static_cast<unsigned long long>(length / 6000),
                        static_cast<unsigned long long>(length / 100 % 60), static_cast<unsigned long long>(length % 100), tape.Frequency());
                    if (ImGui::MenuItem(tape.Playing() ? "Stop" : tape.Recording() ? "Record" : "Play"))
                    {
                        if (tape.Playing()) tape.Stop();
                        else (void)tape.Play();
                    }
                    if (ImGui::MenuItem("Rewind") && !tape.Rewind())
                    { actionError = "Could not rewind the WAV file."; showActionError = true; }
                    if (ImGui::MenuItem("Eject") && !tape.Close())
                    { actionError = "Could not finish the WAV file."; showActionError = true; }
                }
                else
                {
                    if (ImGui::MenuItem("Open WAV...", nullptr, false, !fileDialog->Pending()) &&
                        !sdl_file_dialog::OpenTape(fileDialog, *window))
                    { actionError = "Could not open the file dialog."; showActionError = true; }
                    if (ImGui::MenuItem("Create WAV...", nullptr, false, !fileDialog->Pending()) &&
                        !sdl_file_dialog::SaveTape(fileDialog, *window))
                    { actionError = "Could not open the file dialog."; showActionError = true; }
                }
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }
        if (settings.keyboard)
        {
            ImGui::SetNextWindowSize(ImVec2(950, 260), ImGuiCond_FirstUseEver);
            const bool wasOpen = settings.keyboard;
            if (ImGui::Begin("UKNC Keyboard", &settings.keyboard, ImGuiWindowFlags_HorizontalScrollbar))
            {
                ImGui::TextUnformatted("Shift and Ctrl latch; click again to release. Labels show UKNC Latin positions.");
                ImGui::BeginDisabled(!running || focusLost || !(SDL_GetWindowFlags(window.get()) & SDL_WINDOW_INPUT_FOCUS));
                unsigned previousRow = 0;
                int index = 0;
                for (const auto& key : virtual_keyboard::Keys())
                {
                    if (index && key.row == previousRow) ImGui::SameLine();
                    previousRow = key.row;
                    ImGui::PushID(index++);
                    const bool down = virtualKeys.Down(key.scan);
                    if (down) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                    ImGui::Button(key.label, ImVec2(38.0f * key.width, 30.0f));
                    if (ImGui::IsItemActivated())
                        if (const auto event = virtualKeys.Press(key))
                            queueKey(sdl_keyboard::Source::virtualKey, {event->scan, event->pressed});
                    if (down) ImGui::PopStyleColor();
                    ImGui::PopID();
                }
                ImGui::EndDisabled();
            }
            ImGui::End();
            if (wasOpen != settings.keyboard) { releaseVirtualKeys(); settingsDirty = true; }
        }
        if (showDebugger)
        {
            ImGui::SetNextWindowSize(ImVec2(520, 540), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Debugger", &showDebugger))
            {
                if (ImGui::RadioButton("CPU", debugProcessor == preview_debugger::Processor::cpu))
                    debugProcessor = preview_debugger::Processor::cpu;
                ImGui::SameLine();
                if (ImGui::RadioButton("PPU", debugProcessor == preview_debugger::Processor::ppu))
                    debugProcessor = preview_debugger::Processor::ppu;
                ImGui::SameLine();
                if (ImGui::Button(running ? "Pause" : "Run"))
                { if (running) pauseMachine(); else startMachine(started); }
                ImGui::SameLine();
                ImGui::BeginDisabled(running);
                if (ImGui::Button("Step")) debugger.Step();
                ImGui::SameLine();
                if (ImGui::Button("Step over") && debugger.StepOver(debugProcessor)) startMachine(started);
                ImGui::EndDisabled();
                ImGui::TextDisabled("Step advances both processors, as in the legacy debugger.");
                auto& processor = debugger.GetProcessor(debugProcessor);
                constexpr std::array registerNames{"R0", "R1", "R2", "R3", "R4", "R5", "SP", "PC"};
                for (int reg = 0; reg < 8; ++reg)
                {
                    if (reg % 4) ImGui::SameLine();
                    ImGui::Text("%s: %06o", registerNames[reg], static_cast<unsigned>(processor.GetReg(reg)));
                }
                ImGui::Text("PSW: %06o   CPC: %06o   CPSW: %06o   %s", static_cast<unsigned>(processor.GetPSW()),
                    static_cast<unsigned>(processor.GetCPC()), static_cast<unsigned>(processor.GetCPSW()),
                    processor.IsHaltMode() ? "HALT" : "USER");
                ImGui::SeparatorText("Breakpoints (octal; pause to edit)");
                ImGui::BeginDisabled(running);
                ImGui::SetNextItemWidth(110);
                ImGui::InputText("##breakpoint", breakpointAddress.data(), breakpointAddress.size(), ImGuiInputTextFlags_CharsDecimal);
                ImGui::SameLine();
                if (ImGui::Button("Add"))
                {
                    const auto address = preview_debugger::ParseAddress(breakpointAddress.data());
                    if (!address || !debugger.Breakpoints(debugProcessor).Add(*address))
                        debuggerError = "Enter an even octal address; duplicates and full lists are rejected.";
                    else debuggerError.clear();
                }
                ImGui::SameLine();
                if (ImGui::Button("Clear all")) { debugger.Breakpoints(debugProcessor).Clear(); debuggerError.clear(); }
                if (!debuggerError.empty()) ImGui::TextWrapped("%s", debuggerError.c_str());
                const auto addresses = debugger.Breakpoints(debugProcessor).Addresses();
                std::optional<std::uint16_t> remove;
                for (const auto address : addresses)
                {
                    ImGui::PushID(static_cast<int>(address));
                    ImGui::Text("%06o", static_cast<unsigned>(address)); ImGui::SameLine();
                    if (ImGui::SmallButton("Remove")) remove = address;
                    ImGui::PopID();
                }
                if (remove) (void)debugger.Breakpoints(debugProcessor).Remove(*remove);
                ImGui::EndDisabled();
                ImGui::SeparatorText("Disassembly at PC");
                for (const auto& line : debugger.Disassemble(debugProcessor, processor.GetPC(), 12))
                {
                    const auto ascii = [](const std::wstring& text) {
                        std::string result;
                        for (const auto ch : text) result.push_back(ch >= 0 && ch < 128 ? static_cast<char>(ch) : '?');
                        return result;
                    };
                    const auto mnemonic = ascii(line.instruction.mnemonic);
                    const auto argument = ascii(line.instruction.argument);
                    ImGui::Text("%06o  %06o  %-8s %s", static_cast<unsigned>(line.address),
                        static_cast<unsigned>(line.word), mnemonic.c_str(), argument.c_str());
                }
                if (ImGui::CollapsingHeader("Memory (octal)"))
                {
                    ImGui::SetNextItemWidth(110);
                    ImGui::InputText("Address", memoryAddress.data(), memoryAddress.size(), ImGuiInputTextFlags_CharsDecimal);
                    if (ImGui::Button("Use PC"))
                        std::snprintf(memoryAddress.data(), memoryAddress.size(), "%06o", static_cast<unsigned>(processor.GetPC()));
                    ImGui::SameLine();
                    if (ImGui::Button("Use SP"))
                        std::snprintf(memoryAddress.data(), memoryAddress.size(), "%06o", static_cast<unsigned>(processor.GetSP()));
                    const auto base = preview_debugger::ParseAddress(memoryAddress.data());
                    if (!base) ImGui::TextDisabled("Enter an even octal address.");
                    else for (unsigned row = 0; row < 8; ++row)
                    {
                        const auto address = static_cast<std::uint16_t>(*base + row * 8);
                        ImGui::Text("%06o  %06o %06o %06o %06o", static_cast<unsigned>(address),
                            static_cast<unsigned>(debugger.ReadWord(debugProcessor, address, false)),
                            static_cast<unsigned>(debugger.ReadWord(debugProcessor, static_cast<std::uint16_t>(address + 2), false)),
                            static_cast<unsigned>(debugger.ReadWord(debugProcessor, static_cast<std::uint16_t>(address + 4), false)),
                            static_cast<unsigned>(debugger.ReadWord(debugProcessor, static_cast<std::uint16_t>(address + 6), false)));
                    }
                }
            }
            ImGui::End();
        }
        if (showActionError) { ImGui::OpenPopup("Error"); showActionError = false; }
        if (ImGui::BeginPopupModal("Error", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextWrapped("%s", actionError.c_str());
            if (ImGui::Button("OK")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        if (toggleFullscreen && !sdl_display::ToggleFullscreen(*window))
        { actionError = "Could not change fullscreen mode."; showActionError = true; }
        if (audio && (speedChanged || volumeChanged) &&
            ((speedChanged && !audio->Reset()) || !audio->Configure(settings.speed, settings.volume)))
        {
            SDL_Log("Audio configuration failed: %s", SDL_GetError());
            audio.reset(); audioStream.reset(); soundEnabled = audioActive = false;
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
        int width = 0, height = 0;
        if (!SDL_GetRenderOutputSize(renderer.get(), &width, &height)) return 1;
        const auto scale = ImGui::GetIO().DisplayFramebufferScale;
        const float top = menuHeight * scale.y;
        const float availableHeight = std::max(0.0f, static_cast<float>(height) - top);
        const float drawWidth = std::min(static_cast<float>(width), availableHeight * 4.0f / 3.0f);
        const float drawHeight = drawWidth * 3.0f / 4.0f;
        const SDL_FRect destination{(width - drawWidth) / 2.0f, top + (availableHeight - drawHeight) / 2.0f, drawWidth, drawHeight};
        if (!settings.mouse || !running || focusLost || ImGui::GetIO().WantCaptureMouse ||
            !(SDL_GetWindowFlags(window.get()) & SDL_WINDOW_INPUT_FOCUS) ||
            SDL_GetMouseFocus() != window.get() || scale.x <= 0 || scale.y <= 0)
            releaseMouse();
        else
        {
            const mouse_input::Rect viewport{destination.x / scale.x, destination.y / scale.y,
                destination.w / scale.x, destination.h / scale.y};
            for (const auto& input : mouseEvents)
            {
                if (input.type == SDL_EVENT_MOUSE_MOTION)
                    mouse.Move(input.motion.x, input.motion.y, viewport);
                else
                {
                    mouse.Move(input.button.x, input.button.y, viewport);
                    if (input.button.button == SDL_BUTTON_LEFT || input.button.button == SDL_BUTTON_RIGHT)
                        mouse.Button(input.button.button == SDL_BUTTON_LEFT, input.type == SDL_EVENT_MOUSE_BUTTON_DOWN);
                }
            }
        }
        if (running && scheduler.Due(started))
        {
            scheduler.FrameStarted(started, settings.speed, FRAMERATE);
            if (const auto key = keyDelivery.Next()) board.KeyboardEvent(key->scan, key->pressed);
            if (settings.mouse)
            {
                const auto sample = mouse.Next();
                board.MouseMove(sample.dx, sample.dy, sample.left, sample.right);
            }
            const bool completed = debugger.RunFrame();
            if (!completed)
            {
                pauseMachine();
                showDebugger = true;
            }
            if (!printer.Check()) { actionError = "Printer output failed and was stopped."; showActionError = true; }
            if (tape.TakeError()) { actionError = "Tape I/O failed and playback or recording was stopped."; showActionError = true; }
            if (completed)
            {
                if (const auto event = autoBoot.FrameCompleted())
                    queueKey(sdl_keyboard::Source::automatic, {event->scan, event->pressed});
                snapshots.FrameCompleted();
                if (smokeTest) ++frames;
                if (audio && audioActive && !audio->SubmitFrame())
                {
                    SDL_Log("Audio submission failed: %s", SDL_GetError());
                    audio.reset(); audioStream.reset(); soundEnabled = audioActive = false;
                }
            }
        }
        const auto palette = video_palette::Get(static_cast<video_palette::Mode>(paletteMode));
        if (copyScreenText)
        {
            const auto text = screen_text::Capture(board);
            if (!text || !SDL_SetClipboardText(text->c_str()))
            { actionError = "Could not copy screen text to the clipboard."; showActionError = true; }
        }
        if (!video::RenderRgb(board, pixels, palette)) return 1;
        const auto image = screenshot_image::Prepare(pixels, displayMode);
        if (!image) return 1;
        if (copyScreenshot || screenshotPath)
        {
            const auto capture = screenshotMode == displayMode ? image : screenshot_image::Prepare(pixels, screenshotMode);
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
                if (screenshotPath && conflicts(*screenshotPath))
                {
                    if (showActionError) actionError += "\n";
                    else actionError.clear();
                    actionError += "The selected file is used by the emulator. Choose another file.";
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
        if (!SDL_SetRenderDrawColor(renderer.get(), 0, 0, 0, 255) || !SDL_RenderClear(renderer.get())) return 1;
        if (drawWidth > 0 && drawHeight > 0 && !screen.Draw(*image, destination)) { SDL_Log("Screen draw failed: %s", SDL_GetError()); return 1; }
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer.get());
        if (!SDL_RenderPresent(renderer.get())) return 1;
        if (smokeTest && frames >= 3) break;
        std::this_thread::sleep_for(scheduler.Delay(frame_pacing::Clock::now(), settings.speed, running));
    }
    if (!tape.Close())
    {
        SDL_Log("Could not finish the WAV file.");
        (void)SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Tape", "Could not finish the WAV file.", window.get());
    }
    if (!printer.Stop())
    {
        SDL_Log("Could not finish the printer output file.");
        (void)SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Printer", "Could not finish the printer output file.", window.get());
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
    std::vector<std::string_view> arguments;
    for (int index = 1; index < argc; ++index) arguments.emplace_back(argv[index]);
    const auto options = preview_options::Parse(arguments);
    if (!options) { std::fprintf(stderr, "%s\nUse --help for usage.\n", options.error().c_str()); return 2; }
    if (options->help)
    {
        std::printf("Usage: ukncbtl-sdl [--run|--paused] [--sound|--no-sound] [--fullscreen|--windowed]\n"
                    "  [--rom PATH] [--floppy 0..3 PATH] [--cartridge 1..2 PATH]\n"
                    "  [--hard-disk 1..2 PATH] [--boot 1..7]\n");
        return 0;
    }
    return Run(*options);
}
