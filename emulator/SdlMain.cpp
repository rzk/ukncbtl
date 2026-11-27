/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "MediaSlots.h"
#include "util/PathIdentity.h"
#include "BackgroundJob.h"
#include "AsyncDiskMount.h"
#include "AutoBoot.h"
#include "DiskImageFile.h"
#include "AsyncDiskImage.h"
#include "AsyncDebuggerExport.h"
#include "AsyncSubtitles.h"
#include "AsyncRoms.h"
#include "DisplayListDump.h"
#include "MouseInput.h"
#include "FramePacing.h"
#include "FrameWorker.h"
#include "PreviewRoms.h"
#include "PreviewPrinter.h"
#include "PreviewOptions.h"
#include "PreviewDebugger.h"
#include "PreviewConsole.h"
#include "PreviewSettings.h"
#include "PreviewMediaRestore.h"
#include "PreviewSnapshot.h"
#include "AsyncSnapshot.h"
#include "PreviewSprite.h"
#include "PreviewTape.h"
#include "ScreenshotImage.h"
#include "AsyncClipboard.h"
#include "ScreenshotFile.h"
#include "AsyncScreenshot.h"
#ifdef UKNCBTL_TEST_HOOKS
#include "TraceTestHooks.h"
#include "PrinterTestHooks.h"
#include "ClipboardTestHooks.h"
#include "emubase/Memory.h"
#endif
#include "ScreenTextCapture.h"
#include "SdlAudio.h"
#include "SdlDisplay.h"
#include "SdlKeyboard.h"
#include "SdlFileDialog.h"
#include "SdlScreenRenderer.h"
#include "SdlShortcuts.h"
#include "SdlSprite.h"
#include "SpriteDecoder.h"
#include "VideoDecoder.h"
#include "VideoPalette.h"
#include "UiFont.h"
#include "UiAppearance.h"
#include "VirtualKeyboard.h"
#include "emubase/Board.h"
#include "emubase/Processor.h"
#include "util/ScreenScale.h"
#include "util/SdlThreadCleanup.h"
#include "util/Utf8Path.h"
#include "util/Profiler.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cwchar>
#include <filesystem>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

// The portable core delegates diagnostics to its host.
void DebugLog(const wchar_t* message) { std::fputws(message, stderr); }

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

struct ImGuiClipboard
{
    ImGuiPlatformIO& io = ImGui::GetPlatformIO();
    decltype(io.Platform_SetClipboardTextFn) previousSetter = io.Platform_SetClipboardTextFn;
    void* previousUserData = io.Platform_ClipboardUserData;

    explicit ImGuiClipboard(screenshot_clipboard::AsyncCopy& clipboard)
    {
        io.Platform_ClipboardUserData = &clipboard;
        io.Platform_SetClipboardTextFn = [](ImGuiContext*, const char* text) {
            auto& copy = *static_cast<screenshot_clipboard::AsyncCopy*>(ImGui::GetPlatformIO().Platform_ClipboardUserData);
            (void)copy.SetText(text);
        };
    }

    ~ImGuiClipboard()
    {
        io.Platform_SetClipboardTextFn = previousSetter;
        io.Platform_ClipboardUserData = previousUserData;
    }
};

void WriteOctalAddress(std::span<char, 16> text, std::uint16_t address)
{
    const auto result = std::format_to_n(text.data(), text.size() - 1, "{:06o}", address);
    *result.out = '\0';
}

int ConsoleHistoryCallback(ImGuiInputTextCallbackData* data)
{
    auto& console = *static_cast<preview_console::Console*>(data->UserData);
    const auto text = console.Navigate(data->EventKey == ImGuiKey_UpArrow ? -1 : 1);
    data->DeleteChars(0, data->BufTextLen);
    if (!text.empty()) data->InsertChars(0, text.data(), text.data() + text.size());
    return 0;
}

#ifdef UKNCBTL_TEST_HOOKS
bool WaitForStartupTestEvent(Uint32 type, const std::atomic<bool>& observed)
{
    SDL_Event event{};
    event.type = type;
    if (!SDL_PushEvent(&event)) return false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!observed.load() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return observed.load();
}
#endif

int Run(const preview_options::Options& options)
{
    const bool smokeTest = options.smokeTest;
#ifdef UKNCBTL_TEST_HOOKS
    const bool testFrameAndPrinterFailure = smokeTest && SDL_getenv("UKNCBTL_TEST_FRAME_AND_PRINTER_FAILURE") != nullptr;
    const bool testStartupPump = smokeTest && SDL_getenv("UKNCBTL_TEST_STARTUP_PUMP") != nullptr;
    const bool testStartupQuit = testStartupPump && SDL_getenv("UKNCBTL_TEST_STARTUP_QUIT") != nullptr;
    const bool testStartupSettings = smokeTest && SDL_getenv("UKNCBTL_TEST_STARTUP_SETTINGS") != nullptr;
    const bool testStartupSettingsQuit = testStartupSettings && SDL_getenv("UKNCBTL_TEST_STARTUP_SETTINGS_QUIT") != nullptr;
    const bool testAudioStartupOverlap = smokeTest && SDL_getenv("UKNCBTL_TEST_AUDIO_STARTUP_OVERLAP") != nullptr;
#else
    constexpr bool testAudioStartupOverlap = false;
#endif
    if (!SDL_Init(SDL_INIT_VIDEO)) { SDL_Log("SDL initialization failed: %s", SDL_GetError()); return 1; }
    SdlRuntime sdl;
    sdl_display::CompletionWake completionWake;
    const auto notifyUi = completionWake.Notifier();
    preview_settings::Settings settings;
    std::optional<std::filesystem::path> settingsPath;
    std::string startupError;
    bool settingsDirty = false;
    if (!smokeTest)
    {
        if (options.settingsFile) settingsPath = *options.settingsFile;
        else
        {
            // Keep the existing settings location so upgrades retain preferences and layout.
            const std::unique_ptr<char, decltype(&SDL_free)> directory(SDL_GetPrefPath("UKNCBTL", "SDL Preview"), SDL_free);
            if (directory) settingsPath = PathFromUtf8(directory.get()) / "settings.ini";
        }
        if (!settingsPath) startupError = "Could not locate application settings. Changes will not be saved.";
    }
    else { settings.keyboard = true; settings.spriteViewer = true; settings.tapeVisible = true; settings.mediaVisible = true; } // Exercise tool windows in the smoke test.
#ifdef UKNCBTL_TEST_HOOKS
    if (testStartupSettings)
    {
        if (!options.settingsFile) return 1;
        settingsPath = *options.settingsFile;
    }
    const Uint32 settingsEvent = testStartupSettings ?
        (testStartupSettingsQuit ? SDL_EVENT_QUIT : SDL_RegisterEvents(1)) : 0;
    const Uint32 startupEvent = testStartupPump ? (testStartupQuit ? SDL_EVENT_QUIT : SDL_RegisterEvents(1)) : 0;
    if ((testStartupSettings && !settingsEvent) || (testStartupPump && !startupEvent)) return 1;
    std::atomic<bool> observedSettingsEvent{false}, observedStartupEvent{false};
#endif
    const auto protectedPath = settingsPath.value_or(std::filesystem::path{});
    const auto loadSettings = [&]() -> std::expected<preview_settings::Settings, IniFileError> {
#ifdef UKNCBTL_TEST_HOOKS
        if (testStartupSettings && !WaitForStartupTestEvent(settingsEvent, observedSettingsEvent))
            return std::unexpected(IniFileError::read_failed);
#endif
        return preview_settings::Load(protectedPath);
    };
    background::Job<std::expected<preview_settings::Settings, IniFileError>> settingsWork(notifyUi);
    if (settingsPath)
    {
        try { settingsWork.Start(loadSettings); }
        catch (const std::system_error&) {} // The settings can still be read on the caller.
    }

    const std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
        SDL_CreateWindow("UKNCBTL", settings.windowWidth, settings.windowHeight,
            SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN), SDL_DestroyWindow);
    if (!window) { SDL_Log("Window creation failed: %s", SDL_GetError()); return 1; }
    const auto showWindow = [&] {
        if (smokeTest || !(SDL_GetWindowFlags(window.get()) & SDL_WINDOW_HIDDEN)) return true;
        if (SDL_ShowWindow(window.get())) return true;
        SDL_Log("Window could not be shown: %s", SDL_GetError());
        return false;
    };
    bool quitDuringStartup = false;
    std::stop_source stopStartupMedia;
    const auto observeStartupEvents = [&] {
        quitDuringStartup = quitDuringStartup || sdl_display::CloseRequested(*window);
        if (quitDuringStartup) stopStartupMedia.request_stop();
#ifdef UKNCBTL_TEST_HOOKS
        if (settingsEvent && SDL_HasEvent(settingsEvent)) observedSettingsEvent.store(true);
        if (startupEvent && SDL_HasEvent(startupEvent)) observedStartupEvent.store(true);
#endif
    };
    const auto waitForStartup = [&](const auto& work, const char* title) {
        if (!work.Pending() || work.Ready()) { observeStartupEvents(); return true; }
        (void)SDL_SetWindowTitle(window.get(), title);
        if (!showWindow()) return false;
        sdl_display::WaitForCompletion(completionWake, [&](auto timeout) { return work.WaitFor(timeout); },
            observeStartupEvents);
        observeStartupEvents();
        (void)SDL_SetWindowTitle(window.get(), "UKNCBTL");
        return true;
    };
    ProcessorRuntime processorRuntime;
    CMotherboard board;
    media_slots::Slots media(board);
    preview_roms::Roms roms(board);
    preview_printer::Session printer(board);
    preview_tape::Session tape(board);
    const auto isSettingsFile = [&](const std::filesystem::path& path) {
        return path_identity::SameFile(path, protectedPath);
    };
    const auto loadStartupMedia = [&]() -> std::expected<preview_media_restore::StartupMedia, std::string> {
#ifdef UKNCBTL_TEST_HOOKS
        if (testStartupPump && !WaitForStartupTestEvent(startupEvent, observedStartupEvent))
            return std::unexpected("Startup did not pump events.");
#endif
        return preview_media_restore::PrepareStartup(settings, options, roms, media, tape, protectedPath, stopStartupMedia.get_token());
    };
    background::Job<std::expected<preview_media_restore::StartupMedia, std::string>> startupWork(notifyUi);
    const auto startMedia = [&] {
        if (settingsPath)
        {
            if (!waitForStartup(settingsWork, "UKNCBTL - Loading settings...")) return false;
            if (quitDuringStartup) return true;
            auto loaded = settingsWork.Pending() ? settingsWork.Take() : loadSettings();
            if (loaded)
            {
                if ((loaded->windowWidth != settings.windowWidth || loaded->windowHeight != settings.windowHeight) &&
                    !SDL_SetWindowSize(window.get(), loaded->windowWidth, loaded->windowHeight))
                { SDL_Log("Could not restore window size: %s", SDL_GetError()); }
                settings = std::move(*loaded);
            }
            else
            {
#ifdef UKNCBTL_TEST_HOOKS
                if (testStartupSettings) return false;
#endif
                settingsPath.reset();
                startupError = "Could not load application settings. Changes will not be saved.";
            }
        }
#ifdef UKNCBTL_TEST_HOOKS
        if (testStartupSettings)
        {
            int width = 0, height = 0;
            if (testStartupSettingsQuit || !observedSettingsEvent.load() || !SDL_SyncWindow(window.get()) ||
                !SDL_GetWindowSize(window.get(), &width, &height) || width != 1024 || height != 768 ||
                settings.uiScale != 125) return false;
        }
#endif
        if (!showWindow()) return false;
        if (options.fullscreen.value_or(false) && !SDL_SetWindowFullscreen(window.get(), true))
        { SDL_Log("Fullscreen request failed: %s", SDL_GetError()); return false; }
        try { startupWork.Start(loadStartupMedia); }
        catch (const std::system_error&) {} // Retry on the caller if thread creation failed.
        return true;
    };

    // Start media early when settings are already available. Otherwise overlap
    // the settings read with renderer and ImGui backend initialization.
    const bool settingsInFlight = settingsWork.Pending() && !settingsWork.Ready();
    if (!settingsInFlight && !startMedia()) return 1;
    if (quitDuringStartup) return 0;

    const std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> renderer(
        SDL_CreateRenderer(window.get(), nullptr), SDL_DestroyRenderer);
    if (!renderer) { SDL_Log("Renderer creation failed: %s", SDL_GetError()); return 1; }

    ImGuiRuntime gui;
    IMGUI_CHECKVERSION();
    if (!ImGui::CreateContext()) return 1;
    ImGui::GetIO().IniFilename = nullptr;
    gui.platform = ImGui_ImplSDL3_InitForSDLRenderer(window.get(), renderer.get());
    if (!gui.platform) { SDL_Log("ImGui SDL initialization failed"); return 1; }
    gui.renderer = ImGui_ImplSDLRenderer3_Init(renderer.get());
    if (!gui.renderer) { SDL_Log("ImGui renderer initialization failed"); return 1; }

    if (settingsInFlight && !startMedia()) return 1;
    if (quitDuringStartup) return 0;
    if (!ui_font::Install()) { SDL_Log("Could not load the UI font."); return 1; }
    if (!ui_appearance::Apply(settings.uiTheme, settings.uiScale)) return 1;
    int appliedTheme = settings.uiTheme, appliedScale = settings.uiScale;
    if (!settings.uiLayout.empty()) ImGui::LoadIniSettingsFromMemory(settings.uiLayout.data(), settings.uiLayout.size());
    bool resetUiLayout = false;
    const auto captureUiLayout = [&](preview_settings::Settings& destination) {
        if (resetUiLayout) { destination.uiLayout.clear(); return true; }
        std::size_t size = 0;
        const char* data = ImGui::SaveIniSettingsToMemory(&size);
        if (!data || size > preview_settings::MaxUiLayoutBytes) return false;
        destination.uiLayout.assign(data, size);
        return true;
    };
    if (!waitForStartup(startupWork, "UKNCBTL - Loading media...")) return 1;
    std::expected<preview_media_restore::StartupMedia, std::string> startup;
    try { startup = startupWork.Pending() ? startupWork.Take() : loadStartupMedia(); }
    catch (const std::exception& error)
    { SDL_Log("Could not prepare startup media: %s", error.what()); return 1; }
    if (quitDuringStartup) return 0;
#ifdef UKNCBTL_TEST_HOOKS
    if (testStartupQuit) return 1;
#endif
    if (!startup) { SDL_Log("%s", startup.error().c_str()); return 1; }
    const auto& restoreErrors = startup->restoreErrors;
    if (!restoreErrors.empty())
    {
        if (!startupError.empty()) startupError += "\n";
        startupError += "Could not restore saved media:";
        for (const auto& item : restoreErrors) startupError += "\n" + item;
        startupError += "\nThe saved paths are kept for the next launch.";
    }
    preview_debugger::Session debugger(board);
    if (smokeTest) debugger.CaptureComparison();
    for (const auto address : settings.cpuBreakpoints)
        if (address != BreakpointSet::NoBreakpoint) (void)debugger.Breakpoints(preview_debugger::Processor::cpu).Add(address);
    for (const auto address : settings.ppuBreakpoints)
        if (address != BreakpointSet::NoBreakpoint) (void)debugger.Breakpoints(preview_debugger::Processor::ppu).Add(address);
    preview_console::Console console;
    bool commandSystem = options.rom.has_value();
    bool commandTape = options.tape.has_value();
    std::array<bool, 4> commandFloppies{};
    std::array<bool, 2> commandCartridges{}, commandHardDisks{};
    for (std::size_t index = 0; index < commandFloppies.size(); ++index) commandFloppies[index] = options.floppies[index].has_value();
    for (std::size_t index = 0; index < commandCartridges.size(); ++index)
    { commandCartridges[index] = options.cartridges[index].has_value(); commandHardDisks[index] = options.hardDisks[index].has_value(); }
    const auto fileDialog = std::make_shared<sdl_file_dialog::State>(notifyUi);
    std::string actionError = startupError;
    bool showActionError = !startupError.empty();
    const auto reportActionError = [&](std::string_view message) {
        if (showActionError && !actionError.empty()) actionError += "\n";
        else actionError.clear();
        actionError += message;
        showActionError = true;
    };
    board.Reset();
    board.SetSoundAY(settings.soundAY);
    board.SetSoundCovox(settings.soundCovox);
    board.SetMouse(settings.mouse);
    board.SetNetStation(settings.networkStation);
    if (smokeTest) debugger.SetHistoryEnabled(true);
    preview_snapshot::Session snapshots(board);
    sdl_audio::Playback audio(board);
    sdl_audio::AsyncDeviceOpen audioOpen(notifyUi);
    background::Job<std::vector<sdl_audio::DeviceInfo>> audioDeviceScan(notifyUi);
    std::vector<sdl_audio::DeviceInfo> audioDevices;
    bool audioDevicesStale = true;
    bool audioDeviceLost = false;
    enum class AudioOpenPurpose { startup, selection, recovery };
    auto audioOpenPurpose = AudioOpenPurpose::startup;
    const bool initialSound = options.sound.value_or(settings.sound);
    const auto reportStartupAudioFailure = [&](const char* error) {
        SDL_Log("Audio unavailable: %s", error);
        if (!smokeTest && initialSound)
            reportActionError("Audio is unavailable. Use Audio > Output to retry.");
    };
    const auto requestAudioOpen = [&](SDL_AudioDeviceID device, AudioOpenPurpose purpose) {
        if (!(SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) && !SDL_InitSubSystem(SDL_INIT_AUDIO)) return false;
        if (!audioOpen.Start(device, settings.speed, settings.volume)) return false;
        audioOpenPurpose = purpose;
        return true;
    };
    if (!requestAudioOpen(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, AudioOpenPurpose::startup))
        reportStartupAudioFailure(SDL_GetError());
    if (smokeTest && !testAudioStartupOverlap)
    {
        if (!audioOpen.Pending() || !waitForStartup(audioOpen, "UKNCBTL - Opening audio output...")) return 1;
        if (quitDuringStartup) return 0;
        auto opened = audioOpen.Finish();
        if (!opened->prepared || !audio.Install(std::move(opened->prepared), settings.speed, settings.volume, false))
        {
            reportStartupAudioFailure(opened->error.empty() ? SDL_GetError() : opened->error.c_str());
            return 1;
        }
    }
    bool soundEnabled = initialSound;
    std::vector<std::uint32_t> pixels(video::Width * video::Height);
    video::MemorySnapshot videoMemory;
    std::vector<std::uint32_t> displayPixels;
    std::optional<std::pair<int, int>> preparedVideoModes;
    bool screenNeedsUpload = true;
    // The frame worker and UI also need CPU time while decoding overlaps them.
    sdl_display::CompletionWake videoWake(false);
    video::ParallelDecoder videoDecoder(std::thread::hardware_concurrency() >= 4, {
        .notify = videoWake.Notifier(),
        .waitUntilReady = [&](FrameWorker& worker) { sdl_display::WaitUntilWorkerReady(worker, videoWake); },
    });
    sdl_screen::Renderer screen(*renderer);
    preview_sprite::Image spriteImage(std::thread::hardware_concurrency() >= 2, notifyUi);
    sdl_sprite::Texture spriteTexture(*renderer);
    std::uint64_t uploadedSpriteRevision = 0;
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
    preview_snapshot::AsyncLoad snapshotLoad(notifyUi);
    preview_roms::AsyncLoad romLoad(notifyUi);
    FrameFailureLatch frameFailure;
    screenshot_file::AsyncSave screenshotSave(notifyUi);
#ifdef UKNCBTL_TEST_HOOKS
    std::atomic<bool> releaseScreenText{false};
#endif
    screenshot_clipboard::AsyncCopy clipboardCopy(notifyUi);
    ImGuiClipboard imguiClipboard(clipboardCopy);
    preview_snapshot::AsyncSave snapshotSave(notifyUi);
    disk_image::AsyncCreate diskCreate(notifyUi);
    debugger_export::AsyncSave debuggerExport(notifyUi);
    subtitles::AsyncLoad subtitleLoad(notifyUi);
    preview_tape::AsyncPrepare tapePrepare(notifyUi);
    media_slots::AsyncDiskMount diskMount(notifyUi);
    bool screenshotErrorUnacknowledged = false;
    bool snapshotErrorUnacknowledged = false;
    bool traceErrorUnacknowledged = false;
    bool diskErrorUnacknowledged = false;
    bool exportErrorUnacknowledged = false;
    bool printerErrorUnacknowledged = false;
    bool tapeCreateErrorUnacknowledged = false;
    constexpr std::string_view tapeCreateError = "Could not create the WAV file.";
    constexpr std::string_view printerError = "Printer output failed and was stopped.";
    const auto reportPrinterError = [&] {
        printerErrorUnacknowledged = true;
        reportActionError(printerError);
    };
    const auto reservesPath = [](const auto& operation, const std::filesystem::path& path) {
        return operation.Pending() && path_identity::SameFile(path, operation.Path());
    };
    const auto conflictsWithOutputOrPendingFile = [&](const std::filesystem::path& path) {
        const auto reserved = [&](const auto&... operations) { return (reservesPath(operations, path) || ...); };
        return reserved(screenshotSave, snapshotSave, snapshotLoad, diskCreate, debuggerExport, subtitleLoad, romLoad, tapePrepare, diskMount) ||
            isSettingsFile(path) || printer.Conflicts(path) || tape.Conflicts(path) || debugger.TraceConflicts(path);
    };
    const auto conflicts = [&](const std::filesystem::path& path) {
        return conflictsWithOutputOrPendingFile(path) || media.Conflicts(path) || roms.Conflicts(path);
    };
    if (options.state)
    {
        if (conflicts(*options.state) || !snapshots.Restore(startup->stateImage, [] {}))
        { SDL_Log("Could not load the requested state; check the file and whether it is already in use."); return 1; }
        startup->stateImage = std::vector<std::uint8_t>{};
        roms.MarkRestoredFromSnapshot();
        printer.Synchronize();
        debugger.ClearHistory();
    }
    bool running = preview_options::StartRunning(options, settings.autostart, !restoreErrors.empty());
    if (options.boot) (void)autoBoot.Start(options.boot);
    frame_pacing::Scheduler scheduler(frame_pacing::Clock::now());
    const auto startMachine = [&](frame_pacing::Clock::time_point now) {
        if (frameFailure.Failed())
        {
            debugger.Stop();
            actionError = "Emulation stopped after a frame error. Reset the machine before running again.";
            showActionError = true;
            return;
        }
        debugger.PrepareRun(); running = true; scheduler.Restart(now);
    };
    bool done = false;
    bool forceQuit = false, showFloppyQuit = false;
    bool showDebugger = smokeTest || settings.debuggerVisible;
    bool showConsole = smokeTest || settings.consoleVisible;
    preview_debugger::Processor debugProcessor = settings.debuggerPpu ? preview_debugger::Processor::ppu : preview_debugger::Processor::cpu;
    int networkStationDraft = settings.networkStation;
    std::string networkStationError;
    std::array<char, 256> consoleInput{};
    bool consoleScroll = true;
    if (smokeTest) (void)console.Submit("h", debugger, debugProcessor, running);
    std::array<char, 16> breakpointAddress{};
    std::array<char, 16> memoryAddress{'0', '0', '0', '0', '0', '0', 0};
    std::array<char, 16> memoryValue{}, memorySearchValue{}, registerValue{};
    struct MemorySearchContext
    {
        preview_debugger::MemorySource source;
        bool bytes, hex;
        std::array<char, 16> address, value;
        bool operator==(const MemorySearchContext&) const = default;
    };
    const auto searchContext = [&] {
        using enum preview_debugger::MemorySource;
        const auto source = static_cast<preview_debugger::MemorySource>(settings.memorySource);
        const auto resolved = source == selected ? (debugProcessor == preview_debugger::Processor::cpu ? cpu : ppu) : source;
        return MemorySearchContext{resolved, settings.memoryBytes, settings.memoryHex, memoryAddress, memorySearchValue};
    };
    std::optional<MemorySearchContext> pendingSearch;
    background::Job<std::optional<std::uint16_t>> memorySearch(notifyUi);
    std::array<char, 16> dumpStart{'0', '0', '0', '0', '0', '0', 0};
    std::array<char, 16> dumpEnd{'1', '7', '7', '7', '7', '7', 0};
    int registerIndex = 0;
    std::array<char, 16> disasmAddress{};
    std::uint16_t disasmBase = 0;
    bool followPc = true;
    std::string disasmError;
    std::array<char, 16> spriteAddress{};
    WriteOctalAddress(spriteAddress, settings.spriteAddress);
    std::string spriteError;
    std::string debuggerError;
    std::string registerError, memoryError;
    bool openMemory = false;
    std::array<bool, 6> mediaReadOnly{};
    int screenshotMode = settings.screenshotMode;
    int displayMode = settings.displayMode;
    int paletteMode = settings.paletteMode;
    sdl_file_dialog::DropBatch dropBatch;
    std::optional<std::string> droppedFile;
    bool showDropChooser = false, dropReadOnly = false;
    if (smokeTest) { droppedFile = "smoke-test.dsk"; showDropChooser = true; }
    // Declared after all frame dependencies so an early return joins the worker first.
#ifdef UKNCBTL_TEST_HOOKS
    const bool testFrameFailure = (smokeTest && SDL_getenv("UKNCBTL_TEST_FRAME_FAILURE") != nullptr) || testFrameAndPrinterFailure;
    const bool testScreenshotFailureExit = smokeTest && SDL_getenv("UKNCBTL_TEST_SCREENSHOT_FAILURE_EXIT") != nullptr;
    const bool testScreenshotErrorMerge = smokeTest && SDL_getenv("UKNCBTL_TEST_SCREENSHOT_ERROR_MERGE") != nullptr;
    const bool testSnapshotFailureExit = smokeTest && SDL_getenv("UKNCBTL_TEST_SNAPSHOT_FAILURE_EXIT") != nullptr;
    const bool testDiskFailureExit = smokeTest && SDL_getenv("UKNCBTL_TEST_DISK_FAILURE_EXIT") != nullptr;
    const bool testSnapshotLoadCancel = smokeTest && SDL_getenv("UKNCBTL_TEST_SNAPSHOT_LOAD_CANCEL") != nullptr;
    const bool testExportFailureExit = smokeTest && SDL_getenv("UKNCBTL_TEST_EXPORT_FAILURE_EXIT") != nullptr;
    const bool testSubtitleCancel = smokeTest && SDL_getenv("UKNCBTL_TEST_SUBTITLE_CANCEL") != nullptr;
    const bool testSubtitleFailure = smokeTest && SDL_getenv("UKNCBTL_TEST_SUBTITLE_FAILURE") != nullptr;
    const bool testRomCancel = smokeTest && SDL_getenv("UKNCBTL_TEST_ROM_CANCEL") != nullptr;
    const bool testRomFailure = smokeTest && SDL_getenv("UKNCBTL_TEST_ROM_FAILURE") != nullptr;
    const bool testClipboardDuringFrame = smokeTest && SDL_getenv("UKNCBTL_TEST_CLIPBOARD_DURING_FRAME") != nullptr;
    const bool testSaveDuringFrame = smokeTest && SDL_getenv("UKNCBTL_TEST_SAVE_DURING_FRAME") != nullptr;
    const bool testDebuggerInspection = smokeTest && SDL_getenv("UKNCBTL_TEST_DEBUGGER_INSPECTION") != nullptr;
    const bool testDebuggerSearchDiscard = testDebuggerInspection && SDL_getenv("UKNCBTL_TEST_DEBUGGER_SEARCH_DISCARD") != nullptr;
    const bool testSlowFrame = testClipboardDuringFrame || testSaveDuringFrame || testDebuggerInspection ||
        (smokeTest && SDL_getenv("UKNCBTL_TEST_SLOW_FRAME") != nullptr);
    const bool testAudioOverlap = smokeTest && SDL_getenv("UKNCBTL_TEST_AUDIO_OVERLAP") != nullptr;
    const bool testAudioOpenExit = testAudioOverlap && SDL_getenv("UKNCBTL_TEST_AUDIO_OPEN_EXIT") != nullptr;
    bool audioSubmittedBeforePresent = false, verifiedAudioOverlap = false, verifiedAudioReplacement = false;
    bool verifiedAudioStartup = false, verifiedAudioOpenExit = false;
    SDL_AudioDeviceID testRemovedAudioDevice = 0;
    const bool testStaticVideo = smokeTest && SDL_getenv("UKNCBTL_TEST_STATIC_VIDEO") != nullptr;
    unsigned staticVideoPhase = 0;
    if (testStaticVideo) running = false;
    const bool testSpriteImage = smokeTest && SDL_getenv("UKNCBTL_TEST_SPRITE_IMAGE") != nullptr;
    unsigned spriteTestPhase = 0;
    if (testSpriteImage)
    {
        running = false;
        settings.spriteViewer = true;
        settings.spriteAddress = 01000;
        settings.spriteWidth = 1;
        settings.spriteMode = 0;
        board.GetCPU()->SetPSW(0);
        board.GetCPU()->GetMemoryController()->SetWord(01000, false, 0x0201);
    }
    const bool testBoardOperations = smokeTest && SDL_getenv("UKNCBTL_TEST_BOARD_OPERATIONS") != nullptr;
    const bool testShutdownPump = smokeTest && SDL_getenv("UKNCBTL_TEST_SHUTDOWN_PUMP") != nullptr;
    const bool testEarlyFrameExit = smokeTest && SDL_getenv("UKNCBTL_TEST_EARLY_FRAME_EXIT") != nullptr;
    const bool testScreenshotConflict = smokeTest && SDL_getenv("UKNCBTL_TEST_SCREENSHOT_CONFLICT") != nullptr;
    const bool testCopyScreenshot = testClipboardDuringFrame || (smokeTest && SDL_getenv("UKNCBTL_TEST_COPY_SCREENSHOT") != nullptr);
    const bool testClipboardSupersede = testCopyScreenshot && SDL_getenv("UKNCBTL_TEST_CLIPBOARD_SUPERSEDE") != nullptr;
    const bool testImGuiClipboard = testClipboardSupersede && SDL_getenv("UKNCBTL_TEST_IMGUI_CLIPBOARD") != nullptr;
    const bool testClipboardExit = testCopyScreenshot && SDL_getenv("UKNCBTL_TEST_CLIPBOARD_EXIT") != nullptr;
    const bool testScreenTextExit = smokeTest && SDL_getenv("UKNCBTL_TEST_SCREEN_TEXT_EXIT") != nullptr;
    constexpr auto clipboardTestText = "Newer screen text";
    const bool testTraceFailure = smokeTest && SDL_getenv("UKNCBTL_TEST_TRACE_FAILURE") != nullptr;
    const bool testTraceFailureExit = smokeTest && SDL_getenv("UKNCBTL_TEST_TRACE_FAILURE_EXIT") != nullptr;
    const bool testTraceRecordFailure = smokeTest && SDL_getenv("UKNCBTL_TEST_TRACE_RECORD_FAILURE") != nullptr;
    const char* testPrinterPath = smokeTest ? SDL_getenv("UKNCBTL_TEST_PRINTER_PATH") : nullptr;
    const bool testPrinterFailureExit = smokeTest && SDL_getenv("UKNCBTL_TEST_PRINTER_FAILURE_EXIT") != nullptr;
    unsigned printerTestFrames = 0;
    const char* testScreenshotPath = smokeTest ? SDL_getenv("UKNCBTL_TEST_SCREENSHOT_PATH") : nullptr;
    const char* testSnapshotPath = smokeTest ? SDL_getenv("UKNCBTL_TEST_SNAPSHOT_PATH") : nullptr;
    const char* testSnapshotLoadPath = smokeTest ? SDL_getenv("UKNCBTL_TEST_SNAPSHOT_LOAD_PATH") : nullptr;
    const char* testDiskPath = smokeTest ? SDL_getenv("UKNCBTL_TEST_DISK_PATH") : nullptr;
    const char* testExportPath = smokeTest ? SDL_getenv("UKNCBTL_TEST_EXPORT_PATH") : nullptr;
    const char* testExportKind = smokeTest ? SDL_getenv("UKNCBTL_TEST_EXPORT_KIND") : nullptr;
    const char* testSubtitlePath = smokeTest ? SDL_getenv("UKNCBTL_TEST_SUBTITLE_PATH") : nullptr;
    const char* testRomPath = smokeTest ? SDL_getenv("UKNCBTL_TEST_ROM_PATH") : nullptr;
    const char* testTapePath = smokeTest ? SDL_getenv("UKNCBTL_TEST_TAPE_PATH") : nullptr;
    const bool testTapeOpenFailure = smokeTest && SDL_getenv("UKNCBTL_TEST_TAPE_OPEN_FAILURE") != nullptr;
    const bool testTapeCreate = smokeTest && SDL_getenv("UKNCBTL_TEST_TAPE_CREATE") != nullptr;
    const bool testTapeCreateExit = testTapeCreate && SDL_getenv("UKNCBTL_TEST_TAPE_CREATE_EXIT") != nullptr;
    const char* testHardMountPath = smokeTest ? SDL_getenv("UKNCBTL_TEST_HARD_MOUNT_PATH") : nullptr;
    const char* testFloppyMountPath = smokeTest ? SDL_getenv("UKNCBTL_TEST_FLOPPY_MOUNT_PATH") : nullptr;
    const char* testDiskMountPath = testFloppyMountPath ? testFloppyMountPath : testHardMountPath;
    const bool testHardMountFailure = smokeTest && SDL_getenv("UKNCBTL_TEST_HARD_MOUNT_FAILURE") != nullptr;
    const bool testRomCartridge = smokeTest && SDL_getenv("UKNCBTL_TEST_ROM_CARTRIDGE") != nullptr;
    const char* testTracePath = smokeTest ? SDL_getenv("UKNCBTL_TEST_TRACE_PATH") : nullptr;
    bool injectedFrameFailure = false, verifiedFrameRecovery = false;
    bool requestedTestScreenshot = false, verifiedScreenshotReservation = false;
    bool verifiedSaveDuringFrame = false;
    bool requestedTestClipboard = false, verifiedTestClipboard = false;
    bool requestedTestClipboardText = false;
    bool verifiedScreenshotConflict = false;
    bool verifiedScreenshotErrorMerge = false;
    bool verifiedSnapshotReservation = false;
    bool verifiedSnapshotLoad = false;
    bool verifiedDiskReservation = false;
    bool verifiedExportReservation = false;
    bool verifiedSubtitleLoad = false;
    bool verifiedRomLoad = false;
    bool verifiedTapeOpen = false;
    bool verifiedDiskMount = false;
    bool verifiedTraceRecovery = false;
    unsigned framesAfterTraceRecovery = 0;
    bool injectedSlowFrame = false, pumpedDuringSlowFrame = false;
    constexpr std::uint16_t testMemoryAddress = 0x2345;
    bool requestedDebuggerSearch = false, verifiedDebuggerSearch = false;
    int receivedSlowEvents = 0;
    std::atomic<bool> releaseSlowFrame{false};
    const Uint32 testSlowEventType = testSlowFrame ? SDL_RegisterEvents(1) : 0;
    if (testSlowFrame && !testSlowEventType) return 1;
    const Uint32 boardOperationTestEvent = testBoardOperations ? SDL_RegisterEvents(1) : 0;
    unsigned pumpedBoardOperations = 0;
    bool verifiedDiskEject = false;
    const auto fileOutputPending = [&] {
        return screenshotSave.Pending() || snapshotSave.Pending() || diskCreate.Pending() || debuggerExport.Pending();
    };
    if (testBoardOperations && (!boardOperationTestEvent ||
        !(testDiskMountPath || testSnapshotPath || testSnapshotLoadPath || testTapePath ||
            testPrinterPath || testTracePath || testExportPath || testAudioOverlap || testDebuggerInspection))) return 1;
    if (testAudioOverlap && (!testBoardOperations || !soundEnabled)) return 1;
    if (testScreenshotConflict && !testScreenshotPath) return 1;
    if (testSaveDuringFrame && !(testScreenshotPath || testSnapshotPath || testDiskPath || testExportPath)) return 1;
    if ((testTraceFailure || testTraceFailureExit || testTraceRecordFailure) && !testTracePath) return 1;
    if (testPrinterFailureExit && !testPrinterPath) return 1;
    if (testSnapshotLoadCancel && !testSnapshotLoadPath) return 1;
    if ((testExportPath == nullptr) != (testExportKind == nullptr)) return 1;
    if ((testSubtitleCancel || testSubtitleFailure) && !testSubtitlePath) return 1;
    if ((testRomCancel || testRomFailure || testRomCartridge) && !testRomPath) return 1;
    if ((testScreenshotFailureExit || testScreenshotErrorMerge) &&
        !screenshotSave.Start({}, screenshot_image::Image{0, 0, {}})) return 1;
    if (testSnapshotFailureExit && !testSnapshotPath && !snapshotSave.Start({}, {})) return 1;
    if (testSnapshotPath)
    {
        if (!fileDialog->Begin()) return 1;
        fileDialog->Complete({media_slots::Kind::floppy, -1, sdl_file_dialog::Status::selected,
            testSnapshotPath, sdl_file_dialog::Purpose::snapshotSave});
    }
    if (testSnapshotLoadPath)
    {
        if (!fileDialog->Begin()) return 1;
        fileDialog->Complete({media_slots::Kind::floppy, -1, sdl_file_dialog::Status::selected,
            testSnapshotLoadPath, sdl_file_dialog::Purpose::snapshotLoad});
    }
    if (testDiskPath)
    {
        if (!fileDialog->Begin()) return 1;
        fileDialog->Complete({media_slots::Kind::floppy, 80, sdl_file_dialog::Status::selected,
            testDiskPath, sdl_file_dialog::Purpose::createDisk});
    }
    if (testExportPath)
    {
        const std::string_view kind(testExportKind);
        if (kind != "memory" && kind != "display" && kind != "console") return 1;
        const auto purpose = kind == "memory" ? sdl_file_dialog::Purpose::memoryDump :
            kind == "display" ? sdl_file_dialog::Purpose::displayListDump : sdl_file_dialog::Purpose::consoleLog;
        if (!fileDialog->Begin()) return 1;
        fileDialog->Complete({media_slots::Kind::floppy, 0, sdl_file_dialog::Status::selected,
            testExportPath, purpose, -1, 0, 0100});
    }
    if (testSubtitlePath)
    {
        if (!fileDialog->Begin()) return 1;
        fileDialog->Complete({media_slots::Kind::floppy, -1, sdl_file_dialog::Status::selected,
            testSubtitlePath, sdl_file_dialog::Purpose::subtitles});
    }
    if (testRomPath)
    {
        if (!fileDialog->Begin()) return 1;
        fileDialog->Complete({media_slots::Kind::floppy, testRomCartridge ? 0 : -1, sdl_file_dialog::Status::selected,
            testRomPath, testRomCartridge ? sdl_file_dialog::Purpose::cartridge : sdl_file_dialog::Purpose::systemRom});
    }
    if (testTapePath)
    {
        if (!fileDialog->Begin()) return 1;
        fileDialog->Complete({media_slots::Kind::floppy, -1, sdl_file_dialog::Status::selected,
            testTapePath, testTapeCreate ? sdl_file_dialog::Purpose::tapeCreate : sdl_file_dialog::Purpose::tapeOpen});
    }
    if (testDiskMountPath)
    {
        if (!fileDialog->Begin()) return 1;
        const auto kind = testFloppyMountPath ? media_slots::Kind::floppy : media_slots::Kind::hardDisk;
        fileDialog->Complete({kind, 0, sdl_file_dialog::Status::selected,
            testDiskMountPath, sdl_file_dialog::Purpose::disk, -1, 0, 0, true});
    }
#else
    constexpr bool testAudioOverlap = false;
#endif
    bool printerOkAfterFrame = true;
    sdl_display::CompletionWake boardWake(false);
    FrameWorker frameWorker([&] {
        UKNC_PROFILE_SCOPE("Emulator frame");
        printerOkAfterFrame = true;
#ifdef UKNCBTL_TEST_HOOKS
        if (testFrameFailure && !injectedFrameFailure)
        { injectedFrameFailure = true; throw std::runtime_error("Injected frame failure"); }
        if (testPrinterPath && printerTestFrames < 2)
        {
            auto* ports = board.GetPPUMemoryController();
            if (printerTestFrames++ == 0) ports->SetWord(0177102, false, 0x80);
            else
            {
                ports->SetWord(0177100, false, 0x42);
                ports->SetWord(0177102, false, 0);
            }
        }
        if (testSlowFrame && !injectedSlowFrame)
        {
            injectedSlowFrame = true;
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            while (!releaseSlowFrame.load() && std::chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
#endif
        const bool completed = debugger.RunFrame();
        if (completed)
        {
            if (!audioDeviceLost && audio.IsOpen() && audio.Active())
            {
                [[maybe_unused]] const bool submitted = audio.SubmitFrame();
#ifdef UKNCBTL_TEST_HOOKS
                if (testAudioOverlap && !audioSubmittedBeforePresent)
                    audioSubmittedBeforePresent = submitted && audio.DrainSubmission();
#endif
            }
            printerOkAfterFrame = printer.Check();
        }
        return completed;
    }, boardWake.Notifier());
    // Exclusive board operations run between frames.
    const auto runBoardOperation = [&](std::function<bool()> operation, [[maybe_unused]] bool gateForTest = true) {
        UKNC_PROFILE_SCOPE("SDL board operation wait");
#ifdef UKNCBTL_TEST_HOOKS
        const bool verifyPump = testBoardOperations && gateForTest;
        std::atomic<bool> pumped{false};
        auto task = [&] {
            if (verifyPump)
            {
                SDL_Event event{};
                event.type = boardOperationTestEvent;
                if (!SDL_PushEvent(&event)) return false;
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
                while (!pumped.load() && std::chrono::steady_clock::now() < deadline)
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                if (!pumped.load()) return false;
            }
            return operation();
        };
        if (!frameWorker.Start(task)) return false;
#else
        if (!frameWorker.Start(std::move(operation))) return false;
#endif
        return sdl_display::WaitForWorker(frameWorker, boardWake, [&] {
#ifdef UKNCBTL_TEST_HOOKS
            if (verifyPump && !pumped.load())
            {
                SDL_Event event{};
                if (SDL_PeepEvents(&event, 1, SDL_GETEVENT, boardOperationTestEvent, boardOperationTestEvent) == 1)
                {
                    ++pumpedBoardOperations;
                    pumped.store(true);
                }
            }
#endif
        }) == FrameWorker::Outcome::completed;
    };
    const auto runAudioOperation = [&](std::function<bool()> operation) {
        return runBoardOperation([operation = std::move(operation)] {
            thread_local const SdlThreadCleanup cleanup;
            return operation();
        }, testAudioOverlap);
    };
    const auto reportAudioError = [&] {
        reportActionError("Audio failed. Use Audio > Output to retry.");
    };
    const auto closeFailedAudio = [&](const char* message) {
        SDL_Log("%s: %s", message, SDL_GetError());
        audio.Close();
        return false;
    };
    const auto updateAudio = [&](bool active, bool speedChanged, bool volumeChanged) {
        if (audioDeviceLost || !audio.IsOpen() || (!speedChanged && !volumeChanged && audio.Active() == active)) return true;
        const bool updated = runAudioOperation([&] {
            if ((speedChanged && !audio.Reset()) ||
                ((speedChanged || volumeChanged) && !audio.Configure(settings.speed, settings.volume)))
                return closeFailedAudio("Audio configuration failed");
            if (!audio.SetActive(active)) return closeFailedAudio("Audio device failed");
            return true;
        });
        if (!updated) reportAudioError();
        return updated;
    };
    const auto flushFloppy = [&] {
        return runBoardOperation([&] { return media.FlushFloppy(); });
    };
    const auto startPrinter = [&](const std::filesystem::path& path) {
        return runBoardOperation([&] { return printer.Start(path); });
    };
    const auto stopPrinter = [&] {
        const bool stopped = runBoardOperation([&] { return printer.Stop(); });
        if (!stopped) reportPrinterError();
        return stopped;
    };
    const auto pauseMachine = [&] {
        running = false;
        releaseKeys(); releaseMouse(); debugger.Stop();
        bool printerOk = !printer.Active() || runBoardOperation([&] { return printer.Flush(); });
#ifdef UKNCBTL_TEST_HOOKS
        if (testFrameAndPrinterFailure) printerOk = false;
#endif
        if (!printerOk) reportPrinterError();
    };
    const auto runDebuggerCommand = [&](const auto& command) {
        if (runBoardOperation([&] { command(); return true; })) return true;
        frameFailure.Observe(FrameWorker::Outcome::failed);
        pauseMachine();
        reportActionError("Debugger command failed. Reset the machine before running again.");
        return false;
    };
    const auto captureComparison = [&] {
        const bool captured = runBoardOperation([&] { debugger.CaptureComparison(); return true; });
        if (!captured) reportActionError("Could not capture the debugger comparison baseline.");
        return captured;
    };
    const auto startMemorySearch = [&](std::uint16_t address, std::uint16_t value, preview_debugger::SearchDirection direction) {
        if (memorySearch.Pending()) return false;
        const auto context = searchContext();
        std::unique_ptr<const preview_debugger::MemoryView> captured;
        if (!runBoardOperation([&] {
            UKNC_PROFILE_SCOPE("Debugger memory capture");
            captured = debugger.CaptureMemoryView(context.source, debugProcessor);
            return true;
        })) return false;
        try
        {
            memorySearch.Start([captured = std::move(captured), address, value, bytes = context.bytes, direction] {
                UKNC_PROFILE_SCOPE("Debugger memory search");
                return captured->Find(address, value, bytes, direction);
            });
            pendingSearch = context;
            memoryError.clear();
            return true;
        }
        catch (const std::exception&) { return false; }
    };
    const auto collectMemorySearch = [&] {
        if (pendingSearch && (!showDebugger || *pendingSearch != searchContext())) pendingSearch.reset();
        if (!memorySearch.Ready()) return false;
        const bool apply = std::exchange(pendingSearch, std::nullopt).has_value();
        try
        {
            const auto found = memorySearch.Take();
            if (apply)
            {
                if (found)
                {
                    WriteOctalAddress(memoryAddress, *found);
                    memoryError.clear();
                }
                else memoryError = "Value not found in the captured memory.";
            }
        }
        catch (const std::exception&) { if (apply) memoryError = "Could not search memory."; }
        return true;
    };
    const auto stepMachine = [&](bool over, frame_pacing::Clock::time_point now) {
        if (frameFailure.Failed()) return;
        bool resume = false;
        if (runDebuggerCommand([&] {
            if (over) resume = debugger.StepOver(debugProcessor);
            else debugger.Step();
        }) && resume) startMachine(now);
    };
    const auto submitConsoleCommand = [&](std::string_view command) {
        auto action = preview_console::Action::none;
        (void)runDebuggerCommand([&] { action = console.Submit(command, debugger, debugProcessor, running); });
        return action;
    };
    const auto startTrace = [&](const std::filesystem::path& path, std::uint16_t mask) {
        return runBoardOperation([&] { return debugger.StartTrace(path, mask); });
    };
    const auto clearTrace = [&] {
        const bool cleared = runBoardOperation([&] { return debugger.ClearTrace(); });
        if (!cleared) reportActionError("Could not clear the trace.");
        return cleared;
    };
    const auto stopTrace = [&] {
        const bool stopped = runBoardOperation([&] { debugger.StopTrace(); return true; });
        if (!stopped) reportActionError("Could not stop the trace.");
        return stopped;
    };
#ifdef UKNCBTL_TEST_HOOKS
    constexpr auto testTraceMask = preview_debugger::TraceInstructionsCpu | preview_debugger::TraceInstructionsPpu;
    if (testTracePath)
    {
        if (!startTrace(PathFromUtf8(testTracePath), testTraceMask)) return 1;
        if (testTraceFailure || testTraceFailureExit) preview_debugger::testing::FailWorkerTraceWrites(true);
        if (testTraceRecordFailure) preview_debugger::testing::ThrowOnTraceRecord(2);
    }
    if (testPrinterPath)
    {
        if (!startPrinter(PathFromUtf8(testPrinterPath))) return 1;
        board.GetCPU()->SetDCLOPin(true);
        board.GetPPU()->SetDCLOPin(true);
        preview_printer::testing::FailWorkerWrites(testPrinterFailureExit);
    }
#endif
    const auto toggleTape = [&] {
        return runBoardOperation([&] {
            if (!tape.Playing()) return tape.Play();
            tape.Stop();
            return true;
        });
    };
    const auto rewindTape = [&] {
        const bool rewound = runBoardOperation([&] { return tape.Rewind(); });
        if (!rewound) reportActionError("Could not rewind the WAV file.");
        return rewound;
    };
    const auto seekTape = [&](std::uint32_t position) {
        const bool moved = runBoardOperation([&] { return tape.Seek(position); });
        if (!moved) reportActionError("Could not seek in the WAV file.");
        return moved;
    };
    const auto closeTape = [&] {
        const bool closed = runBoardOperation([&] { return tape.Close(); });
        if (!tape.Inserted())
        {
            commandTape = false;
            if (settings.restoreMedia) { settings.tapePath.clear(); settingsDirty = true; }
        }
        if (!closed) reportActionError("Could not finish the WAV file.");
        return closed;
    };
    const auto resetMachine = [&] {
        if (!flushFloppy())
        { actionError = "Could not flush floppy changes; reset was canceled."; showActionError = true; return false; }
        snapshotLoad.Cancel();
        pendingSearch.reset();
        if (romLoad.Pending() && romLoad.Slot() == -1) romLoad.Cancel();
        releaseKeys();
        releaseMouse();
        debugger.Stop();
        bool audioReset = true;
        const bool reset = runBoardOperation([&] {
            board.Reset();
            debugger.ClearHistory();
            board.SetMouse(settings.mouse);
            printer.Synchronize();
            snapshots.ResetTracking();
            if (!audioDeviceLost && audio.IsOpen() && !audio.Reset()) audioReset = closeFailedAudio("Audio reset failed");
            return true;
        });
        if (!audioReset) reportAudioError();
        if (!reset)
        {
            frameFailure.Observe(FrameWorker::Outcome::failed);
            running = false;
            reportActionError("Could not reset the emulator.");
            return false;
        }
        frameFailure.Recovered();
        return true;
    };
    unsigned frames = 0;
    const auto handleAudioSubmissionFailure = [&] {
        SDL_Log("Audio submission failed.");
        (void)runAudioOperation([&] { audio.Close(); return true; });
        reportAudioError();
    };
    const auto audioRecoveryFailed = [&] {
        audioDeviceLost = false;
        (void)runAudioOperation([&] { audio.Close(); return true; });
        reportActionError("Audio output was disconnected. Use Audio > Output to retry.");
    };
    const auto startAudioRecovery = [&] {
        if (!requestAudioOpen(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, AudioOpenPurpose::recovery))
            audioRecoveryFailed();
    };
    const auto refreshAudioDevices = [&] {
        if (!audioDevicesStale || audioDeviceScan.Pending() || !(SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO)) return;
        audioDevicesStale = false;
        try
        {
            audioDeviceScan.Start([] {
                thread_local const SdlThreadCleanup cleanup;
                return sdl_audio::PlaybackDevices();
            });
        }
        catch (const std::system_error&) { SDL_Log("Could not start the audio device scan"); }
    };
    using ClipboardCopy = screenshot_clipboard::AsyncCopy;
    constexpr std::string_view clipboardError = "Could not copy the screenshot to the clipboard.";
    constexpr std::string_view screenTextError = "Could not copy screen text to the clipboard.";
    const auto startScreenTextCopy = [&] {
        try
        {
            auto input = std::make_unique<screen_text::CaptureInput>();
            screen_text::Capture(board, *input);
            auto recognize = [input = std::move(input)] { return screen_text::Recognize(*input); };
#ifdef UKNCBTL_TEST_HOOKS
            if (testClipboardSupersede)
            {
                return clipboardCopy.StartText([recognize = std::move(recognize), &releaseScreenText] {
                    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
                    while (!releaseScreenText.load() && std::chrono::steady_clock::now() < deadline)
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    if (!releaseScreenText.load()) return std::optional<std::string>{};
                    return recognize();
                });
            }
#endif
            return clipboardCopy.StartText(std::move(recognize));
        }
        catch (...) { return false; }
    };
#ifdef UKNCBTL_TEST_HOOKS
    const auto imageOnClipboard = [] {
        return SDL_HasClipboardData("image/bmp") && SDL_HasClipboardData("image/png");
    };
    const auto screenTextOnClipboard = [] {
        const std::unique_ptr<char, decltype(&SDL_free)> text(SDL_GetClipboardText(), SDL_free);
        const std::string_view contents(text ? text.get() : "");
        return contents.ends_with("\r\n") && std::ranges::count(contents, '\n') == 26;
    };
#endif
    const auto collectClipboard = [&]([[maybe_unused]] bool duringFrame = false) {
        while (const auto copied = clipboardCopy.Poll())
        {
            if (copied->outcome == ClipboardCopy::Outcome::failed)
                reportActionError(copied->kind == ClipboardCopy::Kind::image ? clipboardError : screenTextError);
#ifdef UKNCBTL_TEST_HOOKS
            if (testCopyScreenshot && copied->kind == ClipboardCopy::Kind::image)
            {
                if (testClipboardDuringFrame ? (!duringFrame || frames != 0) : frames < 3) return false;
                if (testClipboardSupersede)
                {
                    const std::unique_ptr<char, decltype(&SDL_free)> text(SDL_GetClipboardText(), SDL_free);
                    const bool expectedText = testImGuiClipboard ?
                        text && std::string_view(text.get()) == clipboardTestText : screenTextOnClipboard();
                    verifiedTestClipboard = copied->outcome == ClipboardCopy::Outcome::superseded &&
                        expectedText && !imageOnClipboard();
                }
                else verifiedTestClipboard = copied->outcome == ClipboardCopy::Outcome::copied && imageOnClipboard();
                if (!verifiedTestClipboard) return false;
            }
#endif
        }
        return true;
    };
    const auto reportMountError = [&](media_slots::Error error) {
        if (error == media_slots::Error::already_mounted)
            reportActionError("The image is already mounted; shared mounts must all be read-only.");
        else if (error == media_slots::Error::missing_cartridge)
            reportActionError("Insert the matching hard-disk ROM cartridge first.");
        else
            reportActionError("Could not attach the disk image. Check the file and the selected drive.");
    };
    const std::function<void()> finalizeGui = [] {
        UKNC_PROFILE_SCOPE("ImGui draw data");
        ImGui::Render();
    };
    const auto collectFileOutputs = [&] {
        if (const auto saved = screenshotSave.Poll(); saved && !*saved)
        {
            reportActionError("Could not save the screenshot image.");
            screenshotErrorUnacknowledged = true;
#ifdef UKNCBTL_TEST_HOOKS
            if (testScreenshotFailureExit) done = true;
            if (testScreenshotErrorMerge) { actionError = "Another operation failed."; showActionError = true; }
#endif
        }
        if (const auto saved = snapshotSave.Poll(); saved && !*saved)
        {
            reportActionError("Could not save the state image.");
            snapshotErrorUnacknowledged = true;
#ifdef UKNCBTL_TEST_HOOKS
            if (testSnapshotFailureExit) done = true;
#endif
        }
        if (const auto created = diskCreate.Poll(); created && !*created)
        {
            reportActionError("Could not create the disk image.");
            diskErrorUnacknowledged = true;
#ifdef UKNCBTL_TEST_HOOKS
            if (testDiskFailureExit) done = true;
#endif
        }
        if (const auto saved = debuggerExport.Poll(); saved && !*saved)
        {
            reportActionError("Could not save the debugger export. The file may be incomplete.");
            exportErrorUnacknowledged = true;
#ifdef UKNCBTL_TEST_HOOKS
            if (testExportFailureExit) done = true;
#endif
        }
    };
    while (true)
    {
        completionWake.Clear();
        collectFileOutputs();
        if (done)
        {
            const bool canQuit = forceQuit || flushFloppy();
#ifdef UKNCBTL_TEST_HOOKS
            if (testBoardOperations && testDiskMountPath &&
                (!canQuit || !verifiedDiskEject || pumpedBoardOperations != 3)) return 1;
#endif
            if (canQuit) break;
            (void)board.TakeFloppyFailure();
            done = false;
            pauseMachine();
            showFloppyQuit = true;
        }
        if (!collectClipboard()) return 1;
        const int diskSlot = diskMount.Slot();
        const auto diskKind = diskMount.DiskKind();
        if (diskMount.Ready())
        {
            std::optional<std::expected<void, media_slots::Error>> mounted;
            const bool collected = runBoardOperation([&] {
                mounted = diskMount.Finish(media);
                return mounted.has_value();
            });
            const bool attached = collected && mounted->has_value();
            if (!attached) reportMountError(collected ? mounted->error() : media_slots::Error::attach_failed);
            else
            {
                const bool floppy = diskKind == media_slots::Kind::floppy;
                (floppy ? commandFloppies[diskSlot] : commandHardDisks[diskSlot]) = false;
                if (settings.restoreMedia)
                {
                    (floppy ? settings.floppies[diskSlot] : settings.hardDisks[diskSlot]) = *media.Path(diskKind, diskSlot);
                    (floppy ? settings.floppyReadOnly[diskSlot] : settings.hardDiskReadOnly[diskSlot]) =
                        media.RequestedReadOnly(diskKind, diskSlot);
                    settingsDirty = true;
                }
            }
#ifdef UKNCBTL_TEST_HOOKS
            if (testDiskMountPath)
            {
                if (attached == testHardMountFailure || media.Attached(diskKind, diskSlot) != attached ||
                    (attached && !media.ReadOnly(diskKind, diskSlot))) return 1;
                verifiedDiskMount = true;
            }
#endif
        }
        if (auto prepared = tapePrepare.Poll())
        {
            const bool creatingTape = prepared->Mode() == preview_tape::FileMode::recording;
            const bool inserted = std::move(*prepared).InsertInto(tape);
            if (!inserted)
            {
                if (creatingTape) tapeCreateErrorUnacknowledged = true;
                reportActionError(creatingTape ? tapeCreateError : "Could not open the WAV file or its format is unsupported.");
            }
            else
            {
                commandTape = false;
                if (settings.restoreMedia)
                {
                    settings.tapePath = creatingTape ? std::filesystem::path{} : tape.Path();
                    settingsDirty = true;
                }
            }
#ifdef UKNCBTL_TEST_HOOKS
            if (testTapePath)
            {
                if (inserted == testTapeOpenFailure || tape.Inserted() != inserted ||
                    (inserted && (tape.Recording() != testTapeCreate || tape.Length() != (testTapeCreate ? 0u : 2u) ||
                        tape.Frequency() != 44100))) return 1;
                if (testBoardOperations)
                {
                    const auto before = pumpedBoardOperations;
                    if (!inserted || !toggleTape() || !tape.Playing() || !toggleTape() || tape.Playing()) return 1;
                    if (!creatingTape && (!seekTape(1) || tape.Position() != 1)) return 1;
                    if (!rewindTape() || tape.Position() != 0 || !closeTape() || tape.Inserted() ||
                        pumpedBoardOperations - before != (creatingTape ? 4u : 5u)) return 1;
                }
                verifiedTapeOpen = true;
            }
#endif
        }
        if (auto loaded = subtitleLoad.Poll())
        {
            if (!loaded->cancelled && !loaded->items)
                reportActionError("Could not load the UTF-16LE disassembly listing.");
            else if (!loaded->cancelled)
                debugger.SetSubtitles(std::move(loaded->path), std::move(*loaded->items));
#ifdef UKNCBTL_TEST_HOOKS
            if (testSubtitlePath)
            {
                if (loaded->cancelled != testSubtitleCancel ||
                    (!loaded->cancelled && loaded->items.has_value() == testSubtitleFailure) ||
                    (debugger.SubtitlePath() != nullptr) != (!testSubtitleCancel && !testSubtitleFailure)) return 1;
                verifiedSubtitleLoad = true;
            }
#endif
        }
        if (auto loaded = romLoad.Poll())
        {
            if (!loaded->cancelled && !loaded->image)
                reportActionError(loaded->slot == -1 ? "Could not load the system ROM image." : "Could not load the ROM cartridge image.");
            else if (!loaded->cancelled && loaded->slot == -1)
            {
                if (!flushFloppy()) reportActionError("Could not flush floppy changes; ROM change was canceled.");
                else
                {
                    roms.InstallSystem(loaded->path, std::get<preview_roms::SystemImage>(*loaded->image));
                    commandSystem = false;
                    if (settings.restoreMedia) { settings.systemRom = loaded->path; settingsDirty = true; }
                    if (resetMachine()) scheduler.Restart(frame_pacing::Clock::now());
                }
            }
            else if (!loaded->cancelled)
            {
                if (!roms.InstallCartridge(loaded->slot, loaded->path, std::get<preview_roms::CartridgeImage>(*loaded->image)))
                    reportActionError("Could not load the ROM cartridge image.");
                else
                {
                    commandCartridges[loaded->slot] = false;
                    if (settings.restoreMedia) { settings.cartridges[loaded->slot] = loaded->path; settingsDirty = true; }
                }
            }
#ifdef UKNCBTL_TEST_HOOKS
            if (testRomPath)
            {
                if (loaded->cancelled != testRomCancel ||
                    (!loaded->cancelled && loaded->image.has_value() == testRomFailure)) return 1;
                if (!testRomFailure && !testRomCancel &&
                    (testRomCartridge ? !roms.Inserted(0) : roms.Source() != preview_roms::SystemSource::file)) return 1;
                if (testRomCancel && (testRomCartridge ? roms.Inserted(0) : roms.Source() != preview_roms::SystemSource::embedded)) return 1;
                verifiedRomLoad = true;
            }
#endif
        }
        if (auto loaded = snapshotLoad.Poll())
        {
            if (loaded->cancelled)
            {
#ifdef UKNCBTL_TEST_HOOKS
                if (testSnapshotLoadCancel)
                {
                    if (loaded->image || roms.Source() == preview_roms::SystemSource::snapshot) return 1;
                    verifiedSnapshotLoad = true;
                }
#endif
            }
            else if (!loaded->image) reportActionError("Could not load the state image.");
            else if (!flushFloppy())
                reportActionError("Could not flush floppy changes; state load was canceled.");
            else
            {
                // AsyncLoad has already validated the image.
                pendingSearch.reset();
                releaseKeys();
                releaseMouse();
                debugger.Stop();
                running = false;
                bool audioPaused = true;
                const bool restored = runBoardOperation([&] {
                    if (!audioDeviceLost && audio.IsOpen() && !(audio.Active() ? audio.SetActive(false) : audio.Reset()))
                        audioPaused = closeFailedAudio("Audio pause failed");
                    if (!snapshots.Restore(*loaded->image, [] {})) return false;
                    roms.MarkRestoredFromSnapshot();
                    printer.Synchronize();
                    debugger.ClearHistory();
                    return true;
                });
                if (!audioPaused) reportAudioError();
                if (!restored)
                {
                    frameFailure.Observe(FrameWorker::Outcome::failed);
                    reportActionError("Could not load the state image.");
                }
                else
                {
                    frameFailure.Recovered();
#ifdef UKNCBTL_TEST_HOOKS
                    if (testSnapshotLoadPath)
                    {
                        if (testBoardOperations && pumpedBoardOperations != 2) return 1;
                        verifiedSnapshotLoad = true;
                        startMachine(frame_pacing::Clock::now());
                    }
#endif
                }
            }
        }
        bool copyScreenshot = false;
#ifdef UKNCBTL_TEST_HOOKS
        if (testCopyScreenshot && !requestedTestClipboard)
        { copyScreenshot = true; screenshotMode = 3; requestedTestClipboard = true; }
#endif
        bool copyScreenText = false;
#ifdef UKNCBTL_TEST_HOOKS
        if (testScreenTextExit && !requestedTestClipboardText) copyScreenText = true;
        if (testClipboardSupersede && frames >= 2 && !requestedTestClipboardText)
        {
            if (testImGuiClipboard)
            {
                if (!clipboardCopy.ImagePending()) return 1;
                ImGui::SetClipboardText(clipboardTestText);
                requestedTestClipboardText = true;
            }
            else copyScreenText = true;
        }
#endif
        std::vector<sdl_shortcuts::Action> shortcuts;
        bool toggleFullscreen = false;
        bool speedChanged = false;
        bool volumeChanged = false;
        std::optional<std::filesystem::path> screenshotPath;
#ifdef UKNCBTL_TEST_HOOKS
        if (testScreenshotPath && !requestedTestScreenshot && (!testShutdownPump || frames >= 2))
        {
            screenshotPath = PathFromUtf8(testScreenshotPath);
            screenshotMode = 3;
            if (SDL_getenv("UKNCBTL_TEST_SCREENSHOT_MATCH_DISPLAY")) displayMode = screenshotMode;
            requestedTestScreenshot = true;
        }
#endif
        std::vector<SDL_Event> mouseEvents;
        bool focusLost = false;
        bool backgroundCompletedDuringEvents = false;
        UKNC_PROFILE_FRAME();
        UKNC_PROFILE_SCOPE("SDL main loop");
        const auto started = std::chrono::steady_clock::now();
        {
        UKNC_PROFILE_SCOPE("SDL events");
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            if (completionWake.Consume(event.type))
            {
                backgroundCompletedDuringEvents = true;
                continue;
            }
#ifdef UKNCBTL_TEST_HOOKS
            if (testSlowFrame && event.type == testSlowEventType)
            {
                ++receivedSlowEvents;
                if (event.user.code != receivedSlowEvents) return 1;
            }
#endif
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_RENDER_DEVICE_RESET || event.type == SDL_EVENT_RENDER_TARGETS_RESET)
            {
                screen.Invalidate();
                screenNeedsUpload = true;
                spriteTexture.Invalidate();
            }
            if ((event.type == SDL_EVENT_AUDIO_DEVICE_ADDED || event.type == SDL_EVENT_AUDIO_DEVICE_REMOVED) &&
                !event.adevice.recording)
            {
                audioDevicesStale = true;
                if (event.type == SDL_EVENT_AUDIO_DEVICE_REMOVED)
                {
                    audioOpen.DeviceRemoved(event.adevice.which);
                    std::erase_if(audioDevices, [&](const auto& device) { return device.id == event.adevice.which; });
                    if (!done && !audioDeviceLost && audio.MatchesRemoval(event.adevice.which, false))
                    {
                        audioDeviceLost = true;
                        if (!audioOpen.Pending()) startAudioRecovery();
#ifdef UKNCBTL_TEST_HOOKS
                        if (testAudioOpenExit && event.adevice.which == testRemovedAudioDevice)
                        {
                            if (!audioOpen.Pending() || !audioDeviceLost) return 1;
                            verifiedAudioOpenExit = true;
                            done = true;
                        }
#endif
                    }
                }
            }
            if ((event.type == SDL_EVENT_DROP_BEGIN || event.type == SDL_EVENT_DROP_FILE ||
                event.type == SDL_EVENT_DROP_COMPLETE) && event.drop.windowID == SDL_GetWindowID(window.get()))
            {
                if (event.type == SDL_EVENT_DROP_BEGIN) dropBatch.Begin();
                else if (event.type == SDL_EVENT_DROP_FILE) dropBatch.Add(event.drop.data);
                else if (event.type == SDL_EVENT_DROP_COMPLETE)
                {
                    auto path = dropBatch.Finish();
                    if (dropBatch.Rejected())
                    { actionError = "Drop one file at a time."; showActionError = true; }
                    else if (path)
                    {
                        if (fileDialog->Pending() || droppedFile)
                        { actionError = "Finish the current file selection before dropping another file."; showActionError = true; }
                        else { droppedFile = std::move(path); dropReadOnly = false; showDropChooser = true; }
                    }
                }
            }
            if (event.type == SDL_EVENT_KEY_DOWN && event.key.windowID == SDL_GetWindowID(window.get()))
                if (const auto shortcut = sdl_shortcuts::Classify(event.key)) shortcuts.push_back(*shortcut);
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
        }
        if (done) continue;
        if (audioDeviceScan.Ready())
        {
            try
            {
                auto devices = audioDeviceScan.Take();
                if (!audioDevicesStale) audioDevices = std::move(devices);
            }
            catch (const std::exception&) { SDL_Log("Could not scan audio output devices"); }
        }
        bool collectAudioOpen = true;
#ifdef UKNCBTL_TEST_HOOKS
        // Exercise a guest frame while the detached replacement is outstanding.
        collectAudioOpen = !testAudioOverlap || !testRemovedAudioDevice || frames >= 2;
        if (testAudioStartupOverlap)
        {
            collectAudioOpen = frames > 0;
            refreshAudioDevices();
        }
#endif
        if (auto opened = collectAudioOpen ? audioOpen.Poll() : std::nullopt)
        {
#ifdef UKNCBTL_TEST_HOOKS
            const auto before = pumpedBoardOperations;
#endif
            const bool installed = opened->prepared && runAudioOperation([&] {
                return audio.Install(std::move(opened->prepared), settings.speed, settings.volume, running && soundEnabled);
            });
            if (installed)
            {
                if (audioDeviceLost)
                    reportActionError(opened->device == SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK ?
                        "Audio output was disconnected; switched to the system default." :
                        "Audio output was disconnected; switched to the selected device.");
                audioDeviceLost = false;
#ifdef UKNCBTL_TEST_HOOKS
                if (testAudioStartupOverlap && audioOpenPurpose == AudioOpenPurpose::startup)
                    verifiedAudioStartup = frames > 0 && audio.IsOpen() && audio.Active();
                if (testAudioOverlap && testRemovedAudioDevice && !verifiedAudioReplacement)
                {
                    if (!audio.Active() || audio.LogicalDevice() == testRemovedAudioDevice ||
                        frames < 2 || pumpedBoardOperations - before != 1)
                    {
                        SDL_Log("Replacement audio check failed: active=%d old=%u new=%u frames=%u operations=%u",
                            audio.Active(), testRemovedAudioDevice, audio.LogicalDevice(), frames, pumpedBoardOperations - before);
                        return 1;
                    }
                    verifiedAudioReplacement = true;
                }
#endif
            }
            else if (audioDeviceLost)
            {
                if (audioOpenPurpose == AudioOpenPurpose::recovery) audioRecoveryFailed();
                else startAudioRecovery();
            }
            else if (audioOpenPurpose == AudioOpenPurpose::startup)
                reportStartupAudioFailure(opened->error.empty() ? "Could not install audio output" : opened->error.c_str());
            else reportActionError("Could not open the selected audio device.");
        }
        if (!audioDeviceLost && !audio.CheckSubmission()) handleAudioSubmissionFailure();
        if (const auto result = fileDialog->Take())
        {
            if (result->status == sdl_file_dialog::Status::failed)
            {
                actionError = "Could not open the file dialog.";
                showActionError = true;
            }
            else if (result->status == sdl_file_dialog::Status::selected)
            {
                const auto path = PathFromUtf8(result->path);
                if (result->purpose == sdl_file_dialog::Purpose::trace)
                {
                    auto target = path;
                    if (!target.has_extension()) target += ".log";
                    if (conflicts(target) || !startTrace(target, static_cast<std::uint16_t>(result->slot)))
                    { actionError = "Could not start the trace. The selected file may be in use or unavailable."; showActionError = true; }
                }
                else if (result->purpose == sdl_file_dialog::Purpose::memoryDump ||
                    result->purpose == sdl_file_dialog::Purpose::displayListDump ||
                    result->purpose == sdl_file_dialog::Purpose::consoleLog)
                {
                    auto target = path;
                    const bool memory = result->purpose == sdl_file_dialog::Purpose::memoryDump;
                    const bool log = result->purpose == sdl_file_dialog::Purpose::consoleLog;
                    if (!target.has_extension()) target += memory ? ".bin" : ".txt";
                    if (conflicts(target))
                    { actionError = "The selected file is used by the emulator. Choose another file."; showActionError = true; }
                    else if (debuggerExport.Pending()) reportActionError("A debugger export is already being saved.");
                    else
                    {
                        std::optional<debugger_export::AsyncSave::Data> data;
                        const bool captured = runBoardOperation([&] {
                            if (memory)
                            {
                                auto words = debugger.CaptureMemory(static_cast<preview_debugger::MemorySource>(result->slot),
                                    debugProcessor, result->start, result->end);
                                if (!words) return false;
                                data.emplace(std::move(*words));
                            }
                            else if (log) data.emplace(console.Log());
                            else
                            {
                                auto snapshot = std::make_unique<video::MemorySnapshot>();
                                video::CaptureMemory(board, *snapshot);
                                data.emplace(std::move(snapshot));
                            }
                            return true;
                        });
                        if (!captured || !debuggerExport.Start(target, std::move(*data)))
                            reportActionError("Could not start saving the debugger export.");
#ifdef UKNCBTL_TEST_HOOKS
                        else if (testExportPath)
                        {
                            if (!debuggerExport.Pending() || !conflicts(target.parent_path() / "." / target.filename()) ||
                                (testBoardOperations && pumpedBoardOperations != 1)) return 1;
                            verifiedExportReservation = true;
                        }
#endif
                    }
                }
                else if (result->purpose == sdl_file_dialog::Purpose::tapeOpen ||
                    result->purpose == sdl_file_dialog::Purpose::tapeCreate)
                {
                    auto target = path;
                    const bool creating = result->purpose == sdl_file_dialog::Purpose::tapeCreate;
                    if (creating && !target.has_extension()) target += ".wav";
                    if (conflicts(target))
                    { actionError = "The selected file is used by the emulator. Choose another file."; showActionError = true; }
                    else if (tapePrepare.Pending()) reportActionError("A tape file operation is already in progress.");
                    else if (tape.Inserted() || !tapePrepare.Start(target,
                        creating ? preview_tape::FileMode::recording : preview_tape::FileMode::playback))
                        reportActionError(creating ? "Could not start creating the WAV file." : "Could not start opening the WAV file.");
#ifdef UKNCBTL_TEST_HOOKS
                    else if (testTapePath)
                    {
                        if (!conflicts(target.parent_path() / "." / target.filename())) return 1;
                        if (testTapeCreateExit) done = true;
                    }
#endif
                }
                else if (result->purpose == sdl_file_dialog::Purpose::createDisk)
                {
                    auto target = path;
                    if (!target.has_extension()) target += ".dsk";
                    if (conflicts(target))
                    { actionError = "The selected file is used by the emulator. Choose another file."; showActionError = true; }
                    else if (diskCreate.Pending()) reportActionError("A disk image is already being created.");
                    else if (!diskCreate.Start(target, result->slot))
                        reportActionError("Could not start creating the disk image.");
#ifdef UKNCBTL_TEST_HOOKS
                    else if (testDiskPath)
                    {
                        if (!diskCreate.Pending() || !conflicts(target.parent_path() / "." / target.filename())) return 1;
                        verifiedDiskReservation = true;
                    }
#endif
                }
                else if (result->purpose == sdl_file_dialog::Purpose::printer)
                {
                    const auto& target = path;
                    if (conflicts(target))
                    { actionError = "The selected file is used by the emulator. Choose another file."; showActionError = true; }
                    else if (!startPrinter(target))
                    { actionError = "Could not open the printer output file."; showActionError = true; }
                }
                else if (result->purpose != sdl_file_dialog::Purpose::screenshot &&
                    result->purpose != sdl_file_dialog::Purpose::snapshotSave &&
                    conflictsWithOutputOrPendingFile(path))
                { actionError = "The selected file is used for emulator output. Choose another file."; showActionError = true; }
                else if (result->purpose == sdl_file_dialog::Purpose::screenshot)
                {
                    const auto format = result->filter == 1 ? screenshot_format::Format::bmp : screenshot_format::Format::png;
                    screenshotPath = screenshot_file::WithDefaultExtension(path, format);
                }
                else if (result->purpose == sdl_file_dialog::Purpose::subtitles)
                {
                    const auto& source = path;
                    if (subtitleLoad.Pending()) reportActionError("A disassembly listing is already being loaded.");
                    else if (conflicts(source)) reportActionError("The selected file is used by the emulator. Choose another file.");
                    else if (!subtitleLoad.Start(source)) reportActionError("Could not start loading the disassembly listing.");
#ifdef UKNCBTL_TEST_HOOKS
                    else if (testSubtitlePath)
                    {
                        if (!subtitleLoad.Pending() || !conflicts(source.parent_path() / "." / source.filename())) return 1;
                        if (testSubtitleCancel) subtitleLoad.Cancel();
                    }
#endif
                }
                else if (result->purpose == sdl_file_dialog::Purpose::snapshotLoad)
                {
                    if (snapshotLoad.Pending()) reportActionError("A state image is already being loaded.");
                    else if (conflicts(path))
                        reportActionError("The selected file is used by the emulator. Choose another file.");
                    else if (!snapshotLoad.Start(path))
                        reportActionError("Could not start loading the state image.");
                    else if (romLoad.Pending() && romLoad.Slot() == -1) romLoad.Cancel();
#ifdef UKNCBTL_TEST_HOOKS
                    else if (testSnapshotLoadPath)
                    {
                        if (!snapshotLoad.Pending() || !conflicts(path.parent_path() / "." / path.filename())) return 1;
                        if (testSnapshotLoadCancel &&
                            (!resetMachine() || !snapshotLoad.Cancelled() || (testBoardOperations && pumpedBoardOperations != 2))) return 1;
                    }
#endif
                }
                else if (result->purpose == sdl_file_dialog::Purpose::snapshotSave)
                {
                    const auto target = preview_snapshot::WithDefaultExtension(path);
                    if (conflicts(target))
                    {
                        actionError = "The selected file is used by the emulator. Choose another file.";
                        showActionError = true;
                    }
                    else if (snapshotSave.Pending())
                    {
                        actionError = "A state image is already being saved.";
                        showActionError = true;
                    }
                    else
                    {
                        std::vector<std::uint8_t> image;
                        bool flushed = false;
                        const bool captured = runBoardOperation([&] {
                            flushed = media.FlushFloppy();
                            if (!flushed) return false;
                            image = snapshots.Capture();
                            return true;
                        });
                        if (!captured)
                            reportActionError(flushed ? "Could not capture the state image." :
                                "Could not flush floppy changes; state save was canceled.");
                        else if (!snapshotSave.Start(target, std::move(image)))
                            reportActionError("Could not start saving the state image.");
#ifdef UKNCBTL_TEST_HOOKS
                        else if (testSnapshotPath)
                        {
                            const auto alias = target.parent_path() / "." / target.filename();
                            if (!snapshotSave.Pending() || !conflicts(alias) ||
                                (testBoardOperations && pumpedBoardOperations != 1)) return 1;
                            verifiedSnapshotReservation = true;
                        }
#endif
                    }
                }
                else if (result->purpose == sdl_file_dialog::Purpose::systemRom)
                {
                    const auto& source = path;
                    if (romLoad.Pending()) reportActionError("A ROM image is already being loaded.");
                    else if (media.Conflicts(source)) reportActionError("The selected file is used by the emulator. Choose another file.");
                    else if (!romLoad.Start(source, -1)) reportActionError("Could not start loading the system ROM image.");
                    else
                    {
                        snapshotLoad.Cancel();
#ifdef UKNCBTL_TEST_HOOKS
                        if (testRomPath)
                        {
                            if (!conflicts(source.parent_path() / "." / source.filename())) return 1;
                            if (testRomCancel && (!resetMachine() || !romLoad.Cancelled())) return 1;
                        }
#endif
                    }
                }
                else if (result->purpose == sdl_file_dialog::Purpose::cartridge)
                {
                    const auto& source = path;
                    if (romLoad.Pending()) reportActionError("A ROM image is already being loaded.");
                    else if (result->slot < 0 || result->slot >= 2 || roms.Inserted(result->slot) || media.Conflicts(source))
                        reportActionError("Could not load the ROM cartridge image.");
                    else if (!romLoad.Start(source, result->slot)) reportActionError("Could not start loading the ROM cartridge image.");
#ifdef UKNCBTL_TEST_HOOKS
                    else if (testRomPath)
                    {
                        if (!conflicts(source.parent_path() / "." / source.filename())) return 1;
                        if (testRomCancel) romLoad.Cancel();
                    }
#endif
                }
                else
                {
                    const auto& source = path;
                    if (diskMount.Pending()) reportActionError("A disk image is already being opened.");
                    else if (const auto allowed = media.CheckAttach(result->kind, result->slot, source, result->readOnly); !allowed)
                        reportMountError(allowed.error());
                    else if (!diskMount.Start(result->kind, source, result->slot, result->readOnly))
                        reportActionError("Could not start opening the disk image.");
#ifdef UKNCBTL_TEST_HOOKS
                    else if (testDiskMountPath && !conflicts(source.parent_path() / "." / source.filename())) return 1;
#endif
                }
            }
        }
        if (done) continue;
        if (appliedTheme != settings.uiTheme || appliedScale != settings.uiScale)
        {
            if (ui_appearance::Apply(settings.uiTheme, settings.uiScale))
            { appliedTheme = settings.uiTheme; appliedScale = settings.uiScale; }
        }
        {
        UKNC_PROFILE_SCOPE("ImGui new frame");
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        }
        if (debugger.TraceMask() && settings.traceMask != debugger.TraceMask())
        { settings.traceMask = debugger.TraceMask(); settingsDirty = true; }
        for (const auto shortcut : shortcuts)
        {
            if (!sdl_shortcuts::Allowed(shortcut, running, focusLost || ImGui::GetIO().WantCaptureKeyboard,
                fileDialog->Pending() || ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId))) continue;
            switch (shortcut)
            {
            case sdl_shortcuts::Action::fullscreen: toggleFullscreen = true; break;
            case sdl_shortcuts::Action::toggleRun: if (running) pauseMachine(); else startMachine(started); break;
            case sdl_shortcuts::Action::reset: if (resetMachine()) scheduler.Restart(started); break;
            case sdl_shortcuts::Action::step: stepMachine(false, started); showDebugger = true; break;
            case sdl_shortcuts::Action::stepOver:
                showDebugger = true; stepMachine(true, started); break;
            }
        }
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
        const auto ejectCartridge = [&](int slot) {
            if (!roms.Eject(slot))
            { actionError = "Eject the matching hard disk before removing its cartridge."; showActionError = true; }
            else
            { commandCartridges[slot] = false; if (settings.restoreMedia) { settings.cartridges[slot].clear(); settingsDirty = true; } }
        };
        const auto openCartridge = [&](int slot) {
            if (!sdl_file_dialog::OpenRom(fileDialog, *window, sdl_file_dialog::Purpose::cartridge, slot))
            { actionError = "Could not open the file dialog."; showActionError = true; }
        };
        const auto ejectDisk = [&](media_slots::Kind kind, int slot) {
            if (!runBoardOperation([&] { return media.Eject(kind, slot).has_value(); }))
            { actionError = "Could not eject the disk image."; showActionError = true; }
            else if (kind == media_slots::Kind::floppy)
            { commandFloppies[slot] = false; if (settings.restoreMedia) { settings.floppies[slot].clear(); settings.floppyReadOnly[slot] = false; settingsDirty = true; } }
            else
            { commandHardDisks[slot] = false; if (settings.restoreMedia) { settings.hardDisks[slot].clear(); settings.hardDiskReadOnly[slot] = false; settingsDirty = true; } }
        };
#ifdef UKNCBTL_TEST_HOOKS
        if (testBoardOperations && testDiskMountPath && verifiedDiskMount && !verifiedDiskEject)
        {
            ejectDisk(diskKind, 0);
            if (media.Attached(diskKind, 0) || media.Path(diskKind, 0)) return 1;
            verifiedDiskEject = true;
        }
#endif
        const auto openDisk = [&](media_slots::Kind kind, int slot, bool readOnly) {
            if (kind == media_slots::Kind::hardDisk && !roms.Inserted(slot))
            { actionError = "Insert the matching hard-disk ROM cartridge first."; showActionError = true; }
            else if (!sdl_file_dialog::Open(fileDialog, *window, kind, slot, readOnly))
            { actionError = "Could not open the file dialog."; showActionError = true; }
        };
        float menuHeight = 0, toolbarHeight = 0, statusHeight = 0;
        {
        UKNC_PROFILE_SCOPE("ImGui interface");
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
                if (ImGui::MenuItem("Reset") && resetMachine()) scheduler.Restart(started);
                if (ImGui::MenuItem("Start automatically", nullptr, &settings.autostart)) settingsDirty = true;
                if (ImGui::MenuItem("Restore media on startup", nullptr, &settings.restoreMedia))
                {
                    settingsDirty = true;
                    if (!settings.restoreMedia)
                    { settings.systemRom.clear(); settings.floppies = {}; settings.cartridges = {}; settings.hardDisks = {}; settings.floppyReadOnly = {}; settings.hardDiskReadOnly = {}; settings.tapePath.clear(); }
                    else
                    {
                        if (!commandSystem) if (const auto* path = roms.SystemPath()) settings.systemRom = *path;
                        if (!commandTape && tape.Inserted() && !tape.Recording()) settings.tapePath = tape.Path();
                        for (int slot = 0; slot < 4; ++slot)
                            if (!commandFloppies[slot]) if (const auto* path = media.Path(media_slots::Kind::floppy, slot))
                            { settings.floppies[slot] = *path; settings.floppyReadOnly[slot] = media.RequestedReadOnly(media_slots::Kind::floppy, slot); }
                        for (int slot = 0; slot < 2; ++slot)
                        {
                            if (!commandCartridges[slot]) if (const auto* path = roms.CartridgePath(slot)) settings.cartridges[slot] = *path;
                            if (!commandHardDisks[slot]) if (const auto* path = media.Path(media_slots::Kind::hardDisk, slot))
                            { settings.hardDisks[slot] = *path; settings.hardDiskReadOnly[slot] = media.RequestedReadOnly(media_slots::Kind::hardDisk, slot); }
                        }
                    }
                }
                if (ImGui::BeginMenu("Network station"))
                {
                    ImGui::SetNextItemWidth(120);
                    ImGui::InputInt("Station number", &networkStationDraft);
                    if (ImGui::Button("Apply"))
                    {
                        if (networkStationDraft < 0 || networkStationDraft > 65535)
                            networkStationError = "Enter a station number from 0 to 65535.";
                        else
                        {
                            settings.networkStation = static_cast<std::uint16_t>(networkStationDraft);
                            board.SetNetStation(settings.networkStation);
                            settingsDirty = true;
                            networkStationError.clear();
                        }
                    }
                    if (!networkStationError.empty()) ImGui::TextWrapped("%s", networkStationError.c_str());
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Reset and boot"))
                {
                    for (int selection = 1; selection <= 7; ++selection)
                    {
                        const auto label = "Boot menu " + std::to_string(selection);
                        if (ImGui::MenuItem(label.c_str()))
                        {
                            if (resetMachine())
                            { (void)autoBoot.Start(selection); startMachine(started); }
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
                    if (ImGui::MenuItem("Load ROM and reset...", nullptr, false, !fileDialog->Pending() && !romLoad.Pending()) &&
                        !sdl_file_dialog::OpenRom(fileDialog, *window, sdl_file_dialog::Purpose::systemRom))
                    { actionError = "Could not open the file dialog."; showActionError = true; }
                    if (ImGui::MenuItem("Use embedded ROM and reset", nullptr, false,
                        roms.Source() != preview_roms::SystemSource::embedded))
                    {
                        if (!flushFloppy())
                        { actionError = "Could not flush floppy changes; ROM change was canceled."; showActionError = true; }
                        else { roms.UseEmbedded(); commandSystem = false; if (settings.restoreMedia) { settings.systemRom.clear(); settingsDirty = true; } if (resetMachine()) scheduler.Restart(started); }
                    }
                    ImGui::EndMenu();
                }
                if (romLoad.Pending())
                {
                    if (romLoad.Cancelled()) ImGui::TextDisabled("Canceling ROM load...");
                    else if (ImGui::MenuItem("Cancel ROM load")) romLoad.Cancel();
                }
                if (ImGui::MenuItem("Load state...", nullptr, false, !fileDialog->Pending() && !snapshotLoad.Pending()) &&
                    !sdl_file_dialog::OpenSnapshot(fileDialog, *window))
                {
                    actionError = "Could not open the file dialog.";
                    showActionError = true;
                }
                if (snapshotLoad.Pending())
                {
                    if (snapshotLoad.Cancelled()) ImGui::TextDisabled("Canceling state load...");
                    else if (ImGui::MenuItem("Cancel state load")) snapshotLoad.Cancel();
                }
                if (ImGui::MenuItem("Save state...", nullptr, false, !fileDialog->Pending()) &&
                    !sdl_file_dialog::SaveSnapshot(fileDialog, *window))
                {
                    actionError = "Could not open the file dialog.";
                    showActionError = true;
                }
                if (ImGui::MenuItem("Sound", nullptr, &soundEnabled))
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
                    if (ImGui::BeginMenu("Output"))
                    {
                        refreshAudioDevices();
                        if (audioOpen.Pending()) ImGui::TextDisabled("Opening audio output...");
                        else if (!audio.IsOpen()) ImGui::TextDisabled("Audio output unavailable");
                        const auto chooseDevice = [&](SDL_AudioDeviceID device) {
                            if (!requestAudioOpen(device, AudioOpenPurpose::selection))
                                reportActionError("Could not open the selected audio device.");
                        };
                        ImGui::BeginDisabled(audioOpen.Pending());
                        if (ImGui::MenuItem("System default", nullptr,
                            audio.IsOpen() && audio.Device() == SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK))
                            chooseDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK);
                        for (const auto& device : audioDevices)
                        {
                            ImGui::PushID(static_cast<int>(device.id));
                            if (ImGui::MenuItem(device.name.c_str(), nullptr, audio.IsOpen() && audio.Device() == device.id))
                                chooseDevice(device.id);
                            ImGui::PopID();
                        }
                        ImGui::EndDisabled();
                        ImGui::EndMenu();
                    }
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
                        if (ImGui::MenuItem("Stop output")) (void)stopPrinter();
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
                if (ImGui::BeginMenu("Appearance"))
                {
                    constexpr std::array themes{"Dark", "Light", "Classic"};
                    for (int index = 0; index < static_cast<int>(themes.size()); ++index)
                        if (ImGui::MenuItem(themes[index], nullptr, settings.uiTheme == index))
                        { settings.uiTheme = index; settingsDirty = true; }
                    ImGui::SetNextItemWidth(160);
                    if (ImGui::SliderInt("UI size", &settings.uiScale, 75, 200, "%d%%")) settingsDirty = true;
                    if (ImGui::MenuItem("Reset tool layout on next launch", nullptr, &resetUiLayout)) settingsDirty = true;
                    ImGui::EndMenu();
                }
                if (ImGui::MenuItem("Toolbar", nullptr, &settings.toolbar)) settingsDirty = true;
                if (ImGui::MenuItem("Status bar", nullptr, &settings.statusBar)) settingsDirty = true;
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
                ImGui::MenuItem("Console", nullptr, &showConsole);
                if (ImGui::MenuItem("Sprite viewer", nullptr, &settings.spriteViewer)) settingsDirty = true;
                if (ImGui::BeginMenu("Trace events"))
                {
                    constexpr std::array traceEvents{
                        std::pair{"Floppy controller", preview_debugger::TraceFloppy},
                        std::pair{"CPU instructions", preview_debugger::TraceInstructionsCpu},
                        std::pair{"PPU instructions", preview_debugger::TraceInstructionsPpu},
                        std::pair{"CPU interrupts", preview_debugger::TraceInterruptsCpu},
                        std::pair{"PPU interrupts", preview_debugger::TraceInterruptsPpu}};
                    for (const auto& [label, flag] : traceEvents)
                    {
                        const bool checked = (settings.traceMask & flag) != 0;
                        if (ImGui::MenuItem(label, nullptr, checked, !checked || settings.traceMask != flag))
                        {
                            settings.traceMask ^= flag;
                            if (debugger.TracePath()) (void)debugger.SetTraceMask(settings.traceMask);
                            settingsDirty = true;
                        }
                    }
                    ImGui::EndMenu();
                }
                if (debugger.TracePath())
                {
                    if (ImGui::MenuItem("Clear trace")) (void)clearTrace();
                    if (ImGui::MenuItem("Stop trace")) (void)stopTrace();
                }
                else if (ImGui::MenuItem("Start trace...", nullptr, false, !fileDialog->Pending()) &&
                    !sdl_file_dialog::SaveTrace(fileDialog, *window, settings.traceMask))
                { actionError = "Could not open the file dialog."; showActionError = true; }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Screenshot"))
            {
                if (ImGui::MenuItem("Copy to clipboard", nullptr, false, !clipboardCopy.ImagePending())) copyScreenshot = true;
                if (ImGui::MenuItem("Copy screen text", nullptr, false, !clipboardCopy.TextPending())) copyScreenText = true;
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
                        if (ImGui::MenuItem("Eject")) ejectCartridge(slot);
                    }
                    else if (ImGui::MenuItem("Insert...", nullptr, false, !fileDialog->Pending())) openCartridge(slot);
                    ImGui::EndMenu();
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Disks"))
            {
                if (ImGui::MenuItem("Media manager", nullptr, &settings.mediaVisible)) settingsDirty = true;
                ImGui::Separator();
                if (ImGui::MenuItem("Flush floppy changes") && !flushFloppy())
                { actionError = "Could not flush floppy changes."; showActionError = true; }
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
                        if (ImGui::MenuItem("Eject")) ejectDisk(kind, slot);
                    }
                    else if (diskMount.Pending())
                        ImGui::TextDisabled("Opening disk image...");
                    else for (const bool readOnly : {false, true})
                    {
                        if (ImGui::MenuItem(readOnly ? "Attach read-only..." : "Attach read/write...", nullptr, false, !fileDialog->Pending()))
                            openDisk(kind, slot, readOnly);
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
                if (ImGui::MenuItem("Tape controls", nullptr, &settings.tapeVisible)) settingsDirty = true;
                ImGui::Separator();
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
                        (void)toggleTape();
                    if (ImGui::MenuItem("Rewind")) (void)rewindTape();
                    if (ImGui::MenuItem("Eject")) (void)closeTape();
                }
                else
                {
                    if (tapePrepare.Pending()) ImGui::TextDisabled(tapePrepare.Mode() == preview_tape::FileMode::recording ? "Creating WAV..." : "Opening WAV...");
                    if (ImGui::MenuItem("Open WAV...", nullptr, false, !fileDialog->Pending() && !tapePrepare.Pending()) &&
                        !sdl_file_dialog::OpenTape(fileDialog, *window))
                    { actionError = "Could not open the file dialog."; showActionError = true; }
                    if (ImGui::MenuItem("Create WAV...", nullptr, false, !fileDialog->Pending() && !tapePrepare.Pending()) &&
                        !sdl_file_dialog::SaveTape(fileDialog, *window))
                    { actionError = "Could not open the file dialog."; showActionError = true; }
                }
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }
        const float barHeight = ImGui::GetFrameHeight() + 2 * ImGui::GetStyle().WindowPadding.y;
        toolbarHeight = settings.toolbar ? barHeight : 0.0f;
        statusHeight = settings.statusBar ? barHeight : 0.0f;
        constexpr ImGuiWindowFlags barFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing;
        if (settings.toolbar)
        {
            ImGui::SetNextWindowPos(ImVec2(0, menuHeight));
            ImGui::SetNextWindowSize(ImVec2(ImGui::GetIO().DisplaySize.x, toolbarHeight));
            if (ImGui::Begin("##toolbar", nullptr, barFlags))
            {
                if (ImGui::Button(running ? "Pause" : "Run")) { if (running) pauseMachine(); else startMachine(started); }
                ImGui::SameLine();
                if (ImGui::Button("Reset") && resetMachine()) scheduler.Restart(started);
                ImGui::SameLine();
                if (ImGui::Button("Debugger")) showDebugger = !showDebugger;
                ImGui::SameLine();
                if (ImGui::Button("Fullscreen")) toggleFullscreen = true;
            }
            ImGui::End();
        }
        if (settings.statusBar)
        {
            ImGui::SetNextWindowPos(ImVec2(0, std::max(menuHeight + toolbarHeight, ImGui::GetIO().DisplaySize.y - statusHeight)));
            ImGui::SetNextWindowSize(ImVec2(ImGui::GetIO().DisplaySize.x, statusHeight));
            if (ImGui::Begin("##status", nullptr, barFlags))
            {
                const auto uptime = snapshots.Uptime();
                ImGui::Text("%s  |  Uptime %02u:%02u:%02u  |  %s  |  %s", running ? "Running" : "Paused",
                    uptime / 3600, uptime / 60 % 60, uptime % 60,
                    debugProcessor == preview_debugger::Processor::cpu ? "CPU" : "PPU",
                    audioOpen.Pending() ? "Opening audio output" : !audio.IsOpen() ? "Audio unavailable" :
                    soundEnabled ? "Sound on" : "Sound off");
            }
            ImGui::End();
        }
        if (settings.mediaVisible)
        {
            ImGui::SetNextWindowSize(ImVec2(760, 460), ImGuiCond_FirstUseEver);
            const bool wasOpen = settings.mediaVisible;
            if (ImGui::Begin("Media manager", &settings.mediaVisible))
            {
                ImGui::TextDisabled(settings.restoreMedia ? "Media restoration is enabled." : "Media restoration is disabled.");
                if (ImGui::BeginTable("media-slots", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
                {
                    ImGui::TableSetupColumn("Slot", ImGuiTableColumnFlags_WidthFixed, 100.0f * ImGui::GetStyle().FontScaleMain);
                    ImGui::TableSetupColumn("Image", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 190.0f * ImGui::GetStyle().FontScaleMain);
                    ImGui::TableHeadersRow();
                    const auto showPath = [](const std::filesystem::path* path) {
                        if (!path) { ImGui::TextDisabled("Empty"); return; }
                        const auto text = path->u8string();
                        ImGui::TextWrapped("%s", reinterpret_cast<const char*>(text.c_str()));
                    };
                    for (int slot = 0; slot < 2; ++slot)
                    {
                        ImGui::PushID("cartridge"); ImGui::PushID(slot);
                        ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0);
                        ImGui::Text("Cartridge %d", slot + 1);
                        ImGui::TableSetColumnIndex(1); showPath(roms.CartridgePath(slot));
                        ImGui::TableSetColumnIndex(2);
                        if (roms.Inserted(slot))
                        { if (ImGui::Button("Eject")) ejectCartridge(slot); }
                        else
                        {
                            ImGui::BeginDisabled(fileDialog->Pending());
                            if (ImGui::Button("Insert...")) openCartridge(slot);
                            ImGui::EndDisabled();
                        }
                        ImGui::PopID(); ImGui::PopID();
                    }
                    for (const auto kind : {media_slots::Kind::floppy, media_slots::Kind::hardDisk})
                    {
                        ImGui::PushID(static_cast<int>(kind));
                        const int count = kind == media_slots::Kind::floppy ? 4 : 2;
                        for (int slot = 0; slot < count; ++slot)
                        {
                            ImGui::PushID(slot);
                            ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0);
                            ImGui::Text(kind == media_slots::Kind::floppy ? "Floppy %d" : "Hard disk %d", slot + 1);
                            ImGui::TableSetColumnIndex(1); showPath(media.Path(kind, slot));
                            if (media.ReadOnly(kind, slot)) ImGui::TextDisabled("Read-only");
                            ImGui::TableSetColumnIndex(2);
                            if (media.Attached(kind, slot))
                            { if (ImGui::Button("Eject")) ejectDisk(kind, slot); }
                            else
                            {
                                const auto index = static_cast<std::size_t>(slot + (kind == media_slots::Kind::floppy ? 0 : 4));
                                ImGui::Checkbox("Read-only", &mediaReadOnly[index]);
                                ImGui::SameLine();
                                ImGui::BeginDisabled(fileDialog->Pending() || diskMount.Pending());
                                if (ImGui::Button("Attach...")) openDisk(kind, slot, mediaReadOnly[index]);
                                ImGui::EndDisabled();
                            }
                            ImGui::PopID();
                        }
                        ImGui::PopID();
                    }
                    ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Tape");
                    ImGui::TableSetColumnIndex(1); showPath(tape.Inserted() ? &tape.Path() : nullptr);
                    ImGui::TableSetColumnIndex(2);
                    if (ImGui::Button("Tape controls")) { settings.tapeVisible = true; settingsDirty = true; }
                    ImGui::EndTable();
                }
            }
            ImGui::End();
            if (wasOpen != settings.mediaVisible) settingsDirty = true;
        }
        if (settings.keyboard)
        {
            ImGui::SetNextWindowSize(ImVec2(950, 360), ImGuiCond_FirstUseEver);
            const bool wasOpen = settings.keyboard;
            if (ImGui::Begin("UKNC Keyboard", &settings.keyboard, ImGuiWindowFlags_HorizontalScrollbar))
            {
                const bool latin = (board.GetKeyboardRegister() & KEYB_LAT) != 0;
                ImGui::Text("UKNC mode: %s. Shift and Ctrl latch; click again to release.", latin ? "LAT" : "RUS");
                constexpr std::array legends{"Latin", "Cyrillic", "Both", "Follow UKNC"};
                ImGui::SetNextItemWidth(150);
                if (ImGui::Combo("Legend", &settings.keyboardLegend, legends.data(), static_cast<int>(legends.size()))) settingsDirty = true;
                ImGui::SameLine(); ImGui::SetNextItemWidth(150);
                if (ImGui::SliderInt("Key size", &settings.keyboardScale, 75, 175, "%d%%")) settingsDirty = true;
                const auto legend = settings.keyboardLegend == 3 ?
                    (latin ? virtual_keyboard::Legend::latin : virtual_keyboard::Legend::cyrillic) :
                    static_cast<virtual_keyboard::Legend>(settings.keyboardLegend);
                const float keyScale = static_cast<float>(settings.keyboardScale * settings.uiScale) / 10000.0f;
                const float textHeight = ImGui::GetTextLineHeight() * (legend == virtual_keyboard::Legend::both ? 2.0f : 1.0f) +
                    2.0f * ImGui::GetStyle().FramePadding.y;
                const float keyHeight = std::max(30.0f * keyScale, textHeight);
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
                    const auto label = virtual_keyboard::Label(key, legend);
                    ImGui::Button(label.c_str(), ImVec2(38.0f * key.width * keyScale, keyHeight));
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
        if (settings.tapeVisible)
        {
            ImGui::SetNextWindowSize(ImVec2(520, 260), ImGuiCond_FirstUseEver);
            const bool wasOpen = settings.tapeVisible;
            if (ImGui::Begin("Tape controls", &settings.tapeVisible))
            {
                if (tape.Inserted())
                {
                    const auto path = tape.Path().u8string();
                    ImGui::TextWrapped("%s", reinterpret_cast<const char*>(path.c_str()));
                    ImGui::Text("%s - %s", tape.Recording() ? "Recording file" : "Playback file", tape.Playing() ? "playing" : "stopped");
                    const auto position = tape::Centiseconds(tape.Position(), tape.Frequency());
                    const auto length = tape::Centiseconds(tape.Length(), tape.Frequency());
                    ImGui::Text("%llu:%02llu.%02llu / %llu:%02llu.%02llu  (%d Hz)",
                        static_cast<unsigned long long>(position / 6000), static_cast<unsigned long long>(position / 100 % 60),
                        static_cast<unsigned long long>(position % 100), static_cast<unsigned long long>(length / 6000),
                        static_cast<unsigned long long>(length / 100 % 60), static_cast<unsigned long long>(length % 100), tape.Frequency());
                    std::uint32_t at = tape.Position(), low = 0, high = tape.Length();
                    ImGui::BeginDisabled(tape.Playing() || tape.Recording() || high == 0);
                    ImGui::SetNextItemWidth(-1);
                    if (ImGui::SliderScalar("##tape-position", ImGuiDataType_U32, &at, &low, &high, ""))
                        (void)seekTape(at);
                    ImGui::EndDisabled();
                    if (tape.Recording()) ImGui::TextDisabled("Seeking is unavailable for recordings.");
                    else if (tape.Playing()) ImGui::TextDisabled("Stop playback to seek.");
                    std::array<float, 256> graph{};
                    const auto waveform = tape.Waveform();
                    std::ranges::transform(waveform, graph.begin(), [](std::uint8_t sample) { return static_cast<float>(sample); });
                    ImGui::PlotLines("##tape-signal", graph.data(), static_cast<int>(graph.size()), 0, "Signal", 0.0f, 255.0f, ImVec2(-1, 60));
                    if (ImGui::Button(tape.Playing() ? "Stop" : tape.Recording() ? "Record" : "Play"))
                        (void)toggleTape();
                    ImGui::SameLine();
                    if (ImGui::Button("Rewind")) (void)rewindTape();
                    ImGui::SameLine();
                    if (ImGui::Button("Eject")) (void)closeTape();
                }
                else
                {
                    ImGui::TextUnformatted(tapePrepare.Pending() ?
                        (tapePrepare.Mode() == preview_tape::FileMode::recording ? "Creating WAV..." : "Opening WAV...") : "No tape inserted.");
                    ImGui::BeginDisabled(fileDialog->Pending() || tapePrepare.Pending());
                    if (ImGui::Button("Open WAV...") && !sdl_file_dialog::OpenTape(fileDialog, *window))
                    { actionError = "Could not open the file dialog."; showActionError = true; }
                    ImGui::SameLine();
                    if (ImGui::Button("Create WAV...") && !sdl_file_dialog::SaveTape(fileDialog, *window))
                    { actionError = "Could not open the file dialog."; showActionError = true; }
                    ImGui::EndDisabled();
                }
            }
            ImGui::End();
            if (wasOpen != settings.tapeVisible) settingsDirty = true;
        }
        if (showDebugger)
        {
            ImGui::SetNextWindowSize(ImVec2(520, 540), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Debugger", &showDebugger))
            {
                const auto copyValueMenu = [&](std::uint16_t value, bool byteMode = false) {
                    for (const bool hex : {false, true})
                    {
                        if (ImGui::MenuItem(hex ? "Copy value (hex)" : "Copy value (octal)"))
                        {
                            const auto text = preview_debugger::FormatValue(value, hex, byteMode);
                            if (!clipboardCopy.SetText(text.c_str()))
                            { actionError = "Could not copy the debugger value to the clipboard."; showActionError = true; }
                        }
                    }
                };
                const auto copyAddressMenu = [&](std::uint16_t address) {
                    for (const bool hex : {false, true})
                    {
                        if (ImGui::MenuItem(hex ? "Copy address (hex)" : "Copy address (octal)"))
                        {
                            const auto text = preview_debugger::FormatValue(address, hex);
                            if (!clipboardCopy.SetText(text.c_str()))
                            { actionError = "Could not copy the debugger address to the clipboard."; showActionError = true; }
                        }
                    }
                };
                const auto goMemory = [&](std::uint16_t address) {
                    WriteOctalAddress(memoryAddress, address);
                    if (settings.memorySource != static_cast<int>(preview_debugger::MemorySource::selected))
                    { settings.memorySource = static_cast<int>(preview_debugger::MemorySource::selected); settingsDirty = true; }
                    if ((address & 1) && !settings.memoryBytes) { settings.memoryBytes = true; settingsDirty = true; }
                    openMemory = true;
                };
                if (ImGui::RadioButton("CPU", debugProcessor == preview_debugger::Processor::cpu))
                    debugProcessor = preview_debugger::Processor::cpu;
                ImGui::SameLine();
                if (ImGui::RadioButton("PPU", debugProcessor == preview_debugger::Processor::ppu))
                    debugProcessor = preview_debugger::Processor::ppu;
                ImGui::SameLine();
                if (ImGui::Button(running ? "Pause" : "Run"))
                { if (running) pauseMachine(); else startMachine(started); }
                ImGui::SameLine();
                ImGui::BeginDisabled(running || frameFailure.Failed());
                if (ImGui::Button("Step")) stepMachine(false, started);
                ImGui::SameLine();
                if (ImGui::Button("Step over")) stepMachine(true, started);
                ImGui::EndDisabled();
                ImGui::TextDisabled("Step advances both processors.");
                ImGui::BeginDisabled(running);
                if (ImGui::Button("Capture comparison baseline")) (void)captureComparison();
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::BeginDisabled(!debugger.HasComparison());
                if (ImGui::Button("Clear comparison")) debugger.ClearComparison();
                ImGui::EndDisabled();
                ImGui::TextDisabled(debugger.HasComparison() ? "Highlighted values differ from the captured baseline." :
                    "Pause and capture a baseline to compare registers and memory.");
                auto& processor = debugger.GetProcessor(debugProcessor);
                constexpr std::array registerNames{"R0", "R1", "R2", "R3", "R4", "R5", "SP", "PC"};
                for (int reg = 0; reg < 8; ++reg)
                {
                    ImGui::PushID(reg);
                    if (reg % 4) ImGui::SameLine();
                    const bool changed = debugger.RegisterChanged(debugProcessor, reg);
                    if (changed)
                        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_PlotHistogram));
                    ImGui::Text("%s: %06o", registerNames[reg], static_cast<unsigned>(processor.GetReg(reg)));
                    if (changed) ImGui::PopStyleColor();
                    if (ImGui::BeginPopupContextItem("register-context"))
                    {
                        copyValueMenu(processor.GetReg(reg));
                        if (ImGui::MenuItem("Show address in memory")) goMemory(processor.GetReg(reg));
                        ImGui::EndPopup();
                    }
                    ImGui::PopID();
                }
                const std::array statusValues{processor.GetPSW(), processor.GetCPC(), processor.GetCPSW()};
                constexpr std::array statusNames{"PSW", "CPC", "CPSW"};
                for (int index = 0; index < 3; ++index)
                {
                    ImGui::PushID(index);
                    if (index) ImGui::SameLine();
                    const bool changed = debugger.RegisterChanged(debugProcessor, index + 8);
                    if (changed) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_PlotHistogram));
                    ImGui::Text("%s: %06o", statusNames[index], static_cast<unsigned>(statusValues[index]));
                    if (changed) ImGui::PopStyleColor();
                    if (ImGui::BeginPopupContextItem("status-context"))
                    { copyValueMenu(statusValues[index]); ImGui::EndPopup(); }
                    ImGui::PopID();
                }
                ImGui::SameLine(); ImGui::TextUnformatted(processor.IsHaltMode() ? "HALT" : "USER");
                if (smokeTest) ImGui::SetNextItemOpen(true, ImGuiCond_Always);
                if (ImGui::CollapsingHeader("Edit registers (octal; paused)"))
                {
                    constexpr std::array names{"R0", "R1", "R2", "R3", "R4", "R5", "SP", "PC", "PSW"};
                    ImGui::BeginDisabled(running);
                    ImGui::SetNextItemWidth(90);
                    ImGui::Combo("##register", &registerIndex, names.data(), static_cast<int>(names.size()));
                    ImGui::SameLine(); ImGui::SetNextItemWidth(110);
                    ImGui::InputText("##register-value", registerValue.data(), registerValue.size(), ImGuiInputTextFlags_CharsDecimal);
                    ImGui::SameLine();
                    if (ImGui::Button("Set register"))
                    {
                        const auto value = preview_debugger::ParseWord(registerValue.data());
                        if (!value || !debugger.WriteRegister(debugProcessor, registerIndex, *value))
                            registerError = "Enter an octal value from 000000 to 177777.";
                        else registerError.clear();
                    }
                    ImGui::EndDisabled();
                    if (!registerError.empty()) ImGui::TextWrapped("%s", registerError.c_str());
                }
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
                ImGui::SameLine();
                if (ImGui::Button("Run to"))
                {
                    const auto address = preview_debugger::ParseAddress(breakpointAddress.data());
                    if (!address || !debugger.RunTo(debugProcessor, *address))
                        debuggerError = "Enter an even octal address.";
                    else { debuggerError.clear(); startMachine(started); }
                }
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
                ImGui::SeparatorText("Disassembly");
                ImGui::Checkbox("Follow PC", &followPc);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(100);
                ImGui::InputText("##disasm-address", disasmAddress.data(), disasmAddress.size(), ImGuiInputTextFlags_CharsDecimal);
                ImGui::SameLine();
                if (ImGui::Button("Go"))
                {
                    const auto address = preview_debugger::ParseAddress(disasmAddress.data());
                    if (!address) disasmError = "Enter an even octal address.";
                    else { disasmBase = *address; followPc = false; disasmError.clear(); }
                }
                ImGui::SameLine();
                ImGui::BeginDisabled(fileDialog->Pending() || subtitleLoad.Pending());
                if (ImGui::Button("Load subtitles..."))
                    if (!sdl_file_dialog::OpenSubtitles(fileDialog, *window))
                    { actionError = "Could not open the file dialog."; showActionError = true; }
                ImGui::EndDisabled();
                if (const auto* path = debugger.SubtitlePath())
                {
                    const auto text = path->u8string();
                    ImGui::TextUnformatted(reinterpret_cast<const char*>(text.c_str()));
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Unload subtitles")) { subtitleLoad.Cancel(); debugger.UnloadSubtitles(); }
                }
                if (subtitleLoad.Pending())
                {
                    if (subtitleLoad.Cancelled()) ImGui::TextDisabled("Canceling subtitle load...");
                    else if (ImGui::SmallButton("Cancel subtitle load")) subtitleLoad.Cancel();
                }
                if (!disasmError.empty()) ImGui::TextWrapped("%s", disasmError.c_str());
                const auto hint = debugger.CurrentHint(debugProcessor);
                for (int index = 0; index < hint.count; ++index)
                {
                    const auto text = preview_debugger::ToUtf8(hint.lines[index]).value_or("?");
                    ImGui::TextWrapped("%s", text.c_str());
                }
                const auto baseAddress = followPc ? processor.GetPC() : disasmBase;
                for (const auto& line : debugger.Disassemble(debugProcessor, baseAddress, 12, true))
                {
                    ImGui::PushID(static_cast<int>(line.address));
                    const auto ascii = [](const std::wstring& text) {
                        std::string result;
                        for (const auto ch : text) result.push_back(ch >= 0 && ch < 128 ? static_cast<char>(ch) : '?');
                        return result;
                    };
                    const auto mnemonic = ascii(line.instruction.mnemonic);
                    const auto argument = ascii(line.instruction.argument);
                    if (!line.blockComment.empty()) ImGui::TextUnformatted(line.blockComment.c_str());
                    ImGui::Text("%06o  %06o  %-8s %s %s", static_cast<unsigned>(line.address),
                        static_cast<unsigned>(line.word), mnemonic.c_str(), argument.c_str(), line.comment.c_str());
                    if (ImGui::BeginPopupContextItem("disassembly-context"))
                    {
                        copyAddressMenu(line.address);
                        copyValueMenu(line.word);
                        if (ImGui::MenuItem("Show address in memory")) goMemory(line.address);
                        ImGui::Separator();
                        auto& breakpoints = debugger.Breakpoints(debugProcessor);
                        const bool present = breakpoints.Contains(line.address);
                        if (ImGui::MenuItem(present ? "Remove breakpoint" : "Add breakpoint", nullptr, false, !running && !(line.address & 1)))
                        {
                            const bool changed = present ? breakpoints.Remove(line.address) : breakpoints.Add(line.address);
                            if (!changed) debuggerError = "Could not change the breakpoint; the list may be full.";
                            else debuggerError.clear();
                        }
                        if (ImGui::MenuItem("Run to address", nullptr, false, !running && !(line.address & 1)))
                        {
                            if (debugger.RunTo(debugProcessor, line.address)) startMachine(started);
                            else debuggerError = "Could not run to this address.";
                        }
                        ImGui::EndPopup();
                    }
                    ImGui::PopID();
                }
                if (!followPc)
                {
                    if (ImGui::Button("Previous word")) disasmBase = static_cast<std::uint16_t>(disasmBase - 2);
                    ImGui::SameLine();
                    if (ImGui::Button("Next word")) disasmBase = static_cast<std::uint16_t>(disasmBase + 2);
                }
                if (smokeTest) ImGui::SetNextItemOpen(true, ImGuiCond_Always);
                if (ImGui::CollapsingHeader("Stack (octal)"))
                {
                    const auto sp = static_cast<std::uint16_t>(processor.GetSP() & 0xfffe);
                    for (int offset = -8; offset < 8; ++offset)
                    {
                        const auto address = static_cast<std::uint16_t>(sp + offset * 2);
                        const auto value = debugger.InspectMemory(preview_debugger::MemorySource::selected, debugProcessor, address, false);
                        ImGui::PushID(offset);
                        const auto text = value ? preview_debugger::FormatValue(*value, false) : "------";
                        ImGui::Text("%s %06o  %s", offset == 0 ? ">" : " ", static_cast<unsigned>(address), text.c_str());
                        if (ImGui::BeginPopupContextItem("stack-context"))
                        {
                            copyAddressMenu(address);
                            if (value) copyValueMenu(*value);
                            if (ImGui::MenuItem("Show address in memory")) goMemory(address);
                            ImGui::EndPopup();
                        }
                        ImGui::PopID();
                    }
                }
                if (smokeTest || openMemory) { ImGui::SetNextItemOpen(true, ImGuiCond_Always); openMemory = false; }
                if (ImGui::CollapsingHeader("Memory"))
                {
                    constexpr std::array memorySources{"Selected processor", "CPU", "PPU", "RAM 0", "RAM 1", "RAM 2", "ROM"};
                    ImGui::SetNextItemWidth(180);
                    if (ImGui::Combo("Source", &settings.memorySource, memorySources.data(), static_cast<int>(memorySources.size()))) settingsDirty = true;
                    ImGui::SameLine();
                    if (ImGui::Checkbox("Bytes", &settings.memoryBytes)) settingsDirty = true;
                    ImGui::SameLine();
                    if (ImGui::Checkbox("Hex values", &settings.memoryHex)) settingsDirty = true;
                    ImGui::SetNextItemWidth(110);
                    ImGui::InputText("Address (octal)", memoryAddress.data(), memoryAddress.size(), ImGuiInputTextFlags_CharsDecimal);
                    const auto memorySource = static_cast<preview_debugger::MemorySource>(settings.memorySource);
                    const auto memoryProcessor = memorySource == preview_debugger::MemorySource::cpu ? preview_debugger::Processor::cpu :
                        memorySource == preview_debugger::MemorySource::ppu ? preview_debugger::Processor::ppu : debugProcessor;
                    const auto& addressProcessor = debugger.GetProcessor(memoryProcessor);
                    if (ImGui::Button("Use PC"))
                        WriteOctalAddress(memoryAddress, addressProcessor.GetPC());
                    ImGui::SameLine();
                    if (ImGui::Button("Use SP"))
                        WriteOctalAddress(memoryAddress, addressProcessor.GetSP());
                    const auto oldBase = settings.memoryBytes ? preview_debugger::ParseWord(memoryAddress.data()) :
                        preview_debugger::ParseAddress(memoryAddress.data());
                    ImGui::SameLine();
                    ImGui::BeginDisabled(!oldBase);
                    if (ImGui::Button("Previous page") && oldBase)
                        WriteOctalAddress(memoryAddress, static_cast<std::uint16_t>(*oldBase - 64));
                    ImGui::SameLine();
                    if (ImGui::Button("Next page") && oldBase)
                        WriteOctalAddress(memoryAddress, static_cast<std::uint16_t>(*oldBase + 64));
                    ImGui::EndDisabled();
                    const auto base = settings.memoryBytes ? preview_debugger::ParseWord(memoryAddress.data()) :
                        preview_debugger::ParseAddress(memoryAddress.data());
                    if (!base) ImGui::TextDisabled(settings.memoryBytes ? "Enter an octal address." : "Enter an even octal address.");
                    else for (unsigned row = 0; row < 8; ++row)
                    {
                        const auto address = static_cast<std::uint16_t>(*base + row * 8);
                        ImGui::Text("%06o  ", static_cast<unsigned>(address));
                        const unsigned stride = settings.memoryBytes ? 1 : 2;
                        for (unsigned offset = 0; offset < 8; offset += stride)
                        {
                            const auto cellAddress = static_cast<std::uint16_t>(address + offset);
                            const auto cell = debugger.InspectMemory(static_cast<preview_debugger::MemorySource>(settings.memorySource),
                                debugProcessor, cellAddress, settings.memoryBytes);
                            std::string text;
                            if (!cell) text += settings.memoryBytes ? (settings.memoryHex ? "-- " : "--- ") :
                                (settings.memoryHex ? "---- " : "------ ");
                            else if (settings.memoryHex) text += settings.memoryBytes ? std::format("{:02X} ", *cell) : std::format("{:04X} ", *cell);
                            else text += settings.memoryBytes ? std::format("{:03o} ", *cell) : std::format("{:06o} ", *cell);
                            const bool changed = debugger.MemoryChanged(memorySource, debugProcessor, cellAddress, settings.memoryBytes);
                            ImGui::SameLine(0, 0);
                            if (changed) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_PlotHistogram));
                            ImGui::TextUnformatted(text.c_str());
                            if (changed) ImGui::PopStyleColor();
                            ImGui::PushID(static_cast<int>(cellAddress));
                            if (ImGui::BeginPopupContextItem("memory-context"))
                            {
                                copyAddressMenu(cellAddress);
                                if (cell) copyValueMenu(*cell, settings.memoryBytes);
                                const bool mapped = memorySource == preview_debugger::MemorySource::selected ||
                                    memorySource == preview_debugger::MemorySource::cpu || memorySource == preview_debugger::MemorySource::ppu;
                                if (ImGui::MenuItem("Show in disassembly", nullptr, false, mapped && !(cellAddress & 1)))
                                {
                                    debugProcessor = memoryProcessor;
                                    disasmBase = cellAddress;
                                    followPc = false;
                                }
                                ImGui::EndPopup();
                            }
                            ImGui::PopID();
                        }
                    }
                    ImGui::SetNextItemWidth(110);
                    ImGui::InputText("##memory-search", memorySearchValue.data(), memorySearchValue.size(),
                        settings.memoryHex ? ImGuiInputTextFlags_CharsHexadecimal : ImGuiInputTextFlags_CharsDecimal);
                    ImGui::SameLine();
                    ImGui::BeginDisabled(memorySearch.Pending());
                    const bool findPrevious = ImGui::Button("Find previous");
                    ImGui::SameLine();
                    const bool findNext = ImGui::Button("Find next");
                    ImGui::EndDisabled();
                    if (findPrevious || findNext)
                    {
                        const auto value = preview_debugger::ParseMemoryValue(memorySearchValue.data(), settings.memoryHex, settings.memoryBytes);
                        if (!base || !value) memoryError = "Enter a valid address and search value.";
                        else
                        {
                            const auto direction = findNext ? preview_debugger::SearchDirection::forward : preview_debugger::SearchDirection::backward;
                            if (!startMemorySearch(*base, *value, direction))
                                memoryError = "Could not search memory.";
                        }
                    }
                    if (pendingSearch) ImGui::TextDisabled("Searching captured memory...");
                    ImGui::TextDisabled("Search wraps around; values use the selected byte/word and radix settings.");
                    const auto exportSource = memorySource == preview_debugger::MemorySource::selected ?
                        (debugProcessor == preview_debugger::Processor::cpu ? preview_debugger::MemorySource::cpu : preview_debugger::MemorySource::ppu) : memorySource;
                    ImGui::BeginDisabled(running || memorySource == preview_debugger::MemorySource::rom);
                    ImGui::SetNextItemWidth(110);
                    ImGui::InputText("##memory-value", memoryValue.data(), memoryValue.size(),
                        settings.memoryHex ? ImGuiInputTextFlags_CharsHexadecimal : ImGuiInputTextFlags_CharsDecimal);
                    ImGui::SameLine();
                    if (ImGui::Button("Write RAM at address"))
                    {
                        const auto value = preview_debugger::ParseMemoryValue(memoryValue.data(), settings.memoryHex, settings.memoryBytes);
                        if (!base || !value || !debugger.WriteMemory(memorySource, debugProcessor, *base, *value, settings.memoryBytes))
                            memoryError = "Enter a valid value and writable RAM address.";
                        else memoryError.clear();
                    }
                    ImGui::EndDisabled();
                    ImGui::TextDisabled("Dump uses the selected memory source; inaccessible words are zero.");
                    ImGui::TextDisabled("Range is octal, start inclusive, end exclusive, rounded to whole words.");
                    ImGui::SetNextItemWidth(100);
                    ImGui::InputText("Start", dumpStart.data(), dumpStart.size(), ImGuiInputTextFlags_CharsDecimal);
                    ImGui::SameLine(); ImGui::SetNextItemWidth(100);
                    ImGui::InputText("End", dumpEnd.data(), dumpEnd.size(), ImGuiInputTextFlags_CharsDecimal);
                    ImGui::BeginDisabled(fileDialog->Pending());
                    if (ImGui::Button("Save memory..."))
                    {
                        const auto start = preview_debugger::ParseWord(dumpStart.data());
                        const auto end = preview_debugger::ParseWord(dumpEnd.data());
                        if (!start || !end || *end <= *start)
                            memoryError = "Enter a valid non-empty octal memory range.";
                        else if (!sdl_file_dialog::SaveMemoryDump(fileDialog, *window, exportSource, *start, *end))
                        { actionError = "Could not open the file dialog."; showActionError = true; }
                        else memoryError.clear();
                    }
                    ImGui::EndDisabled();
                    if (!memoryError.empty()) ImGui::TextWrapped("%s", memoryError.c_str());
                }
                if (ImGui::CollapsingHeader("Ports (octal)"))
                {
                    constexpr std::array<std::uint16_t, 2> cpuPorts{0176640, 0176642};
                    constexpr std::array<std::uint16_t, 11> ppuPorts{0177010, 0177012, 0177014, 0177016,
                        0177020, 0177022, 0177024, 0177026, 0177054, 0177700, 0177716};
                    const std::span<const std::uint16_t> ports = debugProcessor == preview_debugger::Processor::cpu ?
                        std::span<const std::uint16_t>(cpuPorts) : std::span<const std::uint16_t>(ppuPorts);
                    for (const auto port : ports)
                    {
                        const auto value = debugger.ReadPort(debugProcessor, port);
                        ImGui::PushID(static_cast<int>(port));
                        ImGui::Text("%06o  %06o", static_cast<unsigned>(port), static_cast<unsigned>(value));
                        if (ImGui::BeginPopupContextItem("port-context"))
                        { copyAddressMenu(port); copyValueMenu(value); ImGui::EndPopup(); }
                        ImGui::PopID();
                    }
                    if (debugProcessor == preview_debugger::Processor::ppu)
                    {
                        ImGui::Text("177710  %06o", static_cast<unsigned>(board.GetTimerStateView()));
                        ImGui::Text("177712  %06o", static_cast<unsigned>(board.GetTimerReloadView()));
                        ImGui::Text("177714  %06o", static_cast<unsigned>(board.GetTimerValueView()));
                    }
                }
                if (smokeTest) ImGui::SetNextItemOpen(true, ImGuiCond_Always);
                if (ImGui::CollapsingHeader("Instruction history"))
                {
                    bool enabled = debugger.HistoryEnabled();
                    if (ImGui::Checkbox("Record instructions and interrupts", &enabled)) debugger.SetHistoryEnabled(enabled);
                    ImGui::SameLine();
                    if (ImGui::Button("Clear history")) debugger.ClearHistory();
                    ImGui::TextDisabled("Recording can reduce emulation speed; newest entries appear first.");
                    const bool cpu = debugProcessor == preview_debugger::Processor::cpu;
                    const auto& history = debugger.History();
                    if (ImGui::BeginChild("##history", ImVec2(0, 180), ImGuiChildFlags_Borders))
                    {
                        ImGuiListClipper clipper;
                        clipper.Begin(history.Length(cpu));
                        while (clipper.Step()) for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index)
                        {
                            const auto& record = history.Get(cpu, index);
                            if (record.interrupt)
                                ImGui::Text("%06o  INT %06o -> %06o  PSW %06o", static_cast<unsigned>(record.pc),
                                    static_cast<unsigned>(record.words[0]), static_cast<unsigned>(record.words[1]),
                                    static_cast<unsigned>(record.words[2]));
                            else
                            {
                                const auto instruction = DisassembleInstruction(record.words, record.pc);
                                std::string text;
                                if (instruction)
                                {
                                    for (const auto ch : instruction->mnemonic) text.push_back(ch >= 0 && ch < 128 ? static_cast<char>(ch) : '?');
                                    text.push_back(' ');
                                    for (const auto ch : instruction->argument) text.push_back(ch >= 0 && ch < 128 ? static_cast<char>(ch) : '?');
                                }
                                ImGui::Text("%06o  %06o  %-20s PSW %06o", static_cast<unsigned>(record.pc),
                                    static_cast<unsigned>(record.words[0]), text.c_str(), static_cast<unsigned>(record.psw));
                            }
                        }
                    }
                    ImGui::EndChild();
                }
                if (smokeTest) ImGui::SetNextItemOpen(true, ImGuiCond_Always);
                if (ImGui::CollapsingHeader("Display list"))
                {
                    ImGui::BeginDisabled(fileDialog->Pending());
                    if (ImGui::Button("Save display list...") && !sdl_file_dialog::SaveDisplayList(fileDialog, *window))
                    { actionError = "Could not open the file dialog."; showActionError = true; }
                    ImGui::EndDisabled();
                    std::array<video::DisplayTag, video::TotalLines> tags{};
                    video::DisplayListCursor cursor;
                    for (auto& tag : tags) tag = cursor.Next(board);
                    if (ImGui::BeginChild("##display-list", ImVec2(0, 180), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar))
                    {
                        ImGuiListClipper clipper;
                        clipper.Begin(static_cast<int>(tags.size()));
                        while (clipper.Step()) for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index)
                        {
                            const auto line = display_list_dump::FormatLine(index, tags[static_cast<std::size_t>(index)]);
                            ImGui::TextUnformatted(line.data(), line.data() + line.size() - 1);
                        }
                    }
                    ImGui::EndChild();
                }
            }
            ImGui::End();
        }
        if (settings.spriteViewer)
        {
            ImGui::SetNextWindowSize(ImVec2(570, 690), ImGuiCond_FirstUseEver);
            const bool wasOpen = settings.spriteViewer;
            if (ImGui::Begin("Sprite viewer", &settings.spriteViewer))
            {
                ImGui::SetNextItemWidth(110);
                if (ImGui::InputText("Address (octal)", spriteAddress.data(), spriteAddress.size(),
                    ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_EnterReturnsTrue))
                {
                    const auto parsed = preview_debugger::ParseWord(spriteAddress.data());
                    if (!parsed) spriteError = "Enter an octal address from 000000 to 177777.";
                    else { settings.spriteAddress = *parsed; settingsDirty = true; spriteError.clear(); }
                }
                if (!spriteError.empty()) ImGui::TextWrapped("%s", spriteError.c_str());
                if (ImGui::SliderInt("Width (bytes)", &settings.spriteWidth, 1, 32)) settingsDirty = true;
                constexpr std::array modes{"Black and white", "Green/red on black", "Cyan/magenta on blue",
                    "Red/green on black", "Magenta/cyan on blue"};
                if (ImGui::Combo("Colors", &settings.spriteMode, modes.data(), static_cast<int>(modes.size()))) settingsDirty = true;
                const auto page = sprite::PageBytes(settings.spriteWidth, settings.spriteMode);
                const auto movePage = [&](int direction) {
                    settings.spriteAddress = static_cast<std::uint16_t>(settings.spriteAddress + direction * page);
                    WriteOctalAddress(spriteAddress, settings.spriteAddress);
                    spriteError.clear(); settingsDirty = true;
                };
                if (ImGui::Button("Previous page")) movePage(-1);
                ImGui::SameLine();
                if (ImGui::Button("Next page")) movePage(1);
                ImGui::TextDisabled("CPU memory, current CPU mode; page: %d bytes", page);
                const preview_sprite::Selection selection{settings.spriteAddress, settings.spriteWidth, settings.spriteMode};
                const bool prepared = spriteImage.Update(board, selection);
                const auto image = spriteImage.Get(selection);
                const bool upload = image && (!spriteTexture.Get() || image->revision != uploadedSpriteRevision);
                if (!prepared || (upload && !spriteTexture.Update(image->pixels)))
                {
                    actionError = "Could not prepare the sprite image."; showActionError = true;
                    settings.spriteViewer = false; settingsDirty = true;
                }
                else if (image)
                {
                    if (upload) uploadedSpriteRevision = image->revision;
                    const auto id = static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(spriteTexture.Get()));
                    ImGui::Image(ImTextureRef(id), ImVec2(sprite::Width * 2.0f, sprite::Height * 2.0f));
                }
                else ImGui::Dummy(ImVec2(sprite::Width * 2.0f, sprite::Height * 2.0f));
#ifdef UKNCBTL_TEST_HOOKS
                if (testSpriteImage && spriteTestPhase < 4)
                {
                    if (!prepared || !settings.spriteViewer) return 1;
                    if (spriteTestPhase == 0 && image)
                    {
                        if (!upload || image->pixels.front() != 0xffffffffu) return 1;
                        ++spriteTestPhase;
                    }
                    else if (spriteTestPhase == 1)
                    {
                        if (!image || upload) return 1;
                        settings.spriteMode = 1;
                        ++spriteTestPhase;
                    }
                    else if (spriteTestPhase == 2 && image)
                    {
                        if (!upload || image->pixels.front() != 0xff00ff00u) return 1;
                        SDL_Event reset{};
                        reset.type = SDL_EVENT_RENDER_DEVICE_RESET;
                        if (!SDL_PushEvent(&reset)) return 1;
                        ++spriteTestPhase;
                    }
                    else if (spriteTestPhase == 3)
                    {
                        if (!image || !upload || image->pixels.front() != 0xff00ff00u) return 1;
                        ++spriteTestPhase;
                        startMachine(started);
                    }
                }
#endif
            }
            ImGui::End();
            if (wasOpen != settings.spriteViewer) settingsDirty = true;
        }
        if (showConsole)
        {
            ImGui::SetNextWindowSize(ImVec2(610, 320), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Debugger console", &showConsole))
            {
                ImGui::TextDisabled("%s selected. Enter h for help; values are octal.",
                    debugProcessor == preview_debugger::Processor::cpu ? "CPU" : "PPU");
                ImGui::SameLine();
                ImGui::BeginDisabled(fileDialog->Pending());
                if (ImGui::SmallButton("Save log...") && !sdl_file_dialog::SaveConsoleLog(fileDialog, *window))
                { actionError = "Could not open the file dialog."; showActionError = true; }
                ImGui::EndDisabled();
                if (ImGui::BeginChild("##console-log", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()),
                    ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar))
                {
                    ImGui::TextUnformatted(console.Log().c_str());
                    if (consoleScroll) { ImGui::SetScrollHereY(1.0f); consoleScroll = false; }
                }
                ImGui::EndChild();
                ImGui::SetNextItemWidth(-1);
                ImGui::BeginDisabled(frameFailure.Failed());
                if (ImGui::InputText("##console-input", consoleInput.data(), consoleInput.size(),
                    ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackHistory, ConsoleHistoryCallback, &console))
                {
                    const auto action = submitConsoleCommand(consoleInput.data());
                    consoleInput.fill(0); consoleScroll = true;
                    if (action == preview_console::Action::run) startMachine(started);
                    else if (action == preview_console::Action::pause) pauseMachine();
                    else if (action == preview_console::Action::saveMemory)
                    {
                        const auto range = console.TakeMemoryRange();
                        if (!range || !sdl_file_dialog::SaveMemoryDump(fileDialog, *window,
                            debugProcessor == preview_debugger::Processor::cpu ? preview_debugger::MemorySource::cpu :
                                preview_debugger::MemorySource::ppu, range->start, range->end))
                        { actionError = "Could not open the file dialog."; showActionError = true; }
                    }
                    else if (action == preview_console::Action::saveDisplayList &&
                        !sdl_file_dialog::SaveDisplayList(fileDialog, *window))
                    { actionError = "Could not open the file dialog."; showActionError = true; }
                    else if (action == preview_console::Action::startTrace)
                    {
                        const auto mask = console.TakeTraceMask();
                        if (!mask || !sdl_file_dialog::SaveTrace(fileDialog, *window, *mask))
                        { actionError = "Could not open the file dialog."; showActionError = true; }
                    }
                    ImGui::SetKeyboardFocusHere(-1);
                }
                ImGui::EndDisabled();
            }
            ImGui::End();
        }
        if (const auto failure = board.TakeFloppyFailure())
        {
            pauseMachine();
            if (showActionError && !actionError.empty()) actionError += "\n";
            else actionError.clear();
            actionError += std::format("Floppy {}: ", failure->slot + 1);
            if (failure->error == FloppyError::readFailed)
                actionError += "Image read failed; the drive is now write-protected.";
            else if (failure->error == FloppyError::writeFailed)
                actionError += "Image write failed; track changes remain in memory.";
            else actionError += "The changed track cannot be represented in this image; changes remain in memory.";
            actionError += " Emulation was paused.";
            showActionError = true;
        }
        if (const auto failure = board.TakeHardDiskFailure())
        {
            pauseMachine();
            if (showActionError && !actionError.empty()) actionError += "\n";
            else actionError.clear();
            actionError += std::format("Hard disk {}: image {} failed. Emulation was paused.",
                failure->slot + 1, failure->error == HardDiskError::readFailed ? "read" : "write");
            if (failure->error == HardDiskError::writeFailed)
                actionError += " The affected sector may not have been saved.";
            showActionError = true;
        }
        if (debugger.PollTraceError() && runBoardOperation([&] { return debugger.TakeTraceError(); }))
        {
            traceErrorUnacknowledged = true;
            if (showActionError && !actionError.empty()) actionError += "\n";
            else actionError.clear();
            actionError += "Trace output failed and tracing was stopped."; showActionError = true;
#ifdef UKNCBTL_TEST_HOOKS
            if (testTraceFailure)
            {
                if (verifiedTraceRecovery || debugger.TracePath() || debugger.TraceMask()) return 1;
                preview_debugger::testing::FailWorkerTraceWrites(false);
                if (!startTrace(PathFromUtf8(testTracePath), testTraceMask)) return 1;
                verifiedTraceRecovery = true;
                traceErrorUnacknowledged = false; // Simulate acknowledging the test dialog.
            }
            if (testTraceFailureExit) done = true;
#endif
        }
        if (showDropChooser) { ImGui::OpenPopup("Open dropped file"); showDropChooser = false; }
        if (ImGui::BeginPopupModal("Open dropped file", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            if (droppedFile)
            {
                ImGui::TextWrapped("%s", droppedFile->c_str());
                ImGui::TextUnformatted("Choose how to use this file:");
                ImGui::Checkbox("Mount disk read-only", &dropReadOnly);
                ImGui::TextDisabled("Eject media first; hard disks also require a cartridge.");
                const auto choose = [&](const char* label, sdl_file_dialog::Purpose purpose,
                    media_slots::Kind kind, int slot, bool enabled) {
                    ImGui::BeginDisabled(!enabled);
                    if (ImGui::Button(label))
                    {
                        if (fileDialog->Begin())
                        {
                            fileDialog->Complete({kind, slot, sdl_file_dialog::Status::selected,
                                *droppedFile, purpose, -1, 0, 0, dropReadOnly});
                            droppedFile.reset();
                            ImGui::CloseCurrentPopup();
                        }
                        else
                        {
                            actionError = "Finish the current file selection and try again.";
                            showActionError = true;
                        }
                    }
                    ImGui::EndDisabled();
                };
                for (int slot = 0; slot < 4; ++slot)
                {
                    if (slot) ImGui::SameLine();
                    const auto label = std::format("Floppy {}", slot);
                    choose(label.c_str(), sdl_file_dialog::Purpose::disk, media_slots::Kind::floppy, slot,
                        !media.Attached(media_slots::Kind::floppy, slot) && !diskMount.Pending());
                }
                for (int slot = 0; slot < 2; ++slot)
                {
                    if (slot) ImGui::SameLine();
                    const auto label = std::format("Hard disk {}", slot + 1);
                    choose(label.c_str(), sdl_file_dialog::Purpose::disk, media_slots::Kind::hardDisk, slot,
                        roms.Inserted(slot) && !media.Attached(media_slots::Kind::hardDisk, slot) && !diskMount.Pending());
                }
                for (int slot = 0; slot < 2; ++slot)
                {
                    if (slot) ImGui::SameLine();
                    const auto label = std::format("Cartridge {}", slot + 1);
                    choose(label.c_str(), sdl_file_dialog::Purpose::cartridge, media_slots::Kind::floppy, slot,
                        !roms.Inserted(slot));
                }
                choose("System ROM and reset", sdl_file_dialog::Purpose::systemRom, media_slots::Kind::floppy, -1, true);
                choose("Load state", sdl_file_dialog::Purpose::snapshotLoad, media_slots::Kind::floppy, -1, true);
                choose("Open tape", sdl_file_dialog::Purpose::tapeOpen, media_slots::Kind::floppy, -1, !tape.Inserted() && !tapePrepare.Pending());
                choose("Load disassembly subtitles", sdl_file_dialog::Purpose::subtitles, media_slots::Kind::floppy, -1, true);
                if (ImGui::Button("Cancel")) { droppedFile.reset(); ImGui::CloseCurrentPopup(); }
            }
            ImGui::EndPopup();
        }
        if (showFloppyQuit) { ImGui::OpenPopup("Unsaved floppy changes"); showFloppyQuit = false; }
        if (ImGui::BeginPopupModal("Unsaved floppy changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextWrapped("The floppy image could not be updated. Track changes are still in memory.\n"
                "Retry after correcting the problem, cancel exit, or exit anyway and risk losing the changes.");
            if (ImGui::Button("Retry and exit"))
            {
                if (flushFloppy()) { (void)board.TakeFloppyFailure(); done = true; ImGui::CloseCurrentPopup(); }
                else (void)board.TakeFloppyFailure();
            }
            ImGui::SameLine();
            if (ImGui::Button("Exit anyway")) { forceQuit = true; done = true; ImGui::CloseCurrentPopup(); }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        if (showActionError)
        {
            constexpr std::string_view screenshotError = "Could not save the screenshot image.";
            constexpr std::string_view snapshotError = "Could not save the state image.";
            constexpr std::string_view traceError = "Trace output failed and tracing was stopped.";
            constexpr std::string_view diskError = "Could not create the disk image.";
            constexpr std::string_view exportError = "Could not save the debugger export. The file may be incomplete.";
            if (screenshotErrorUnacknowledged && actionError.find(screenshotError) == std::string::npos)
            {
                if (!actionError.empty()) actionError += "\n";
                actionError += screenshotError;
            }
            if (snapshotErrorUnacknowledged && actionError.find(snapshotError) == std::string::npos)
            {
                if (!actionError.empty()) actionError += "\n";
                actionError += snapshotError;
            }
            if (traceErrorUnacknowledged && actionError.find(traceError) == std::string::npos)
            {
                if (!actionError.empty()) actionError += "\n";
                actionError += traceError;
            }
            if (diskErrorUnacknowledged && actionError.find(diskError) == std::string::npos)
            {
                if (!actionError.empty()) actionError += "\n";
                actionError += diskError;
            }
            if (exportErrorUnacknowledged && actionError.find(exportError) == std::string::npos)
            {
                if (!actionError.empty()) actionError += "\n";
                actionError += exportError;
            }
            if (printerErrorUnacknowledged && actionError.find(printerError) == std::string::npos)
            {
                if (!actionError.empty()) actionError += "\n";
                actionError += printerError;
            }
            if (tapeCreateErrorUnacknowledged && actionError.find(tapeCreateError) == std::string::npos)
            {
                if (!actionError.empty()) actionError += "\n";
                actionError += tapeCreateError;
            }
#ifdef UKNCBTL_TEST_HOOKS
            if (testScreenshotErrorMerge && screenshotErrorUnacknowledged)
            {
                if (actionError.find("Another operation failed.") == std::string::npos ||
                    actionError.find(screenshotError) == std::string::npos) return 1;
                verifiedScreenshotErrorMerge = true;
                screenshotErrorUnacknowledged = false; // Simulate acknowledging the test dialog.
            }
#endif
            ImGui::OpenPopup("Error"); showActionError = false;
        }
        if (ImGui::BeginPopupModal("Error", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextWrapped("%s", actionError.c_str());
            if (ImGui::Button("OK"))
            {
                if (actionError.find("Could not save the screenshot image.") != std::string::npos)
                    screenshotErrorUnacknowledged = false;
                if (actionError.find("Could not save the state image.") != std::string::npos)
                    snapshotErrorUnacknowledged = false;
                if (actionError.find("Trace output failed and tracing was stopped.") != std::string::npos)
                    traceErrorUnacknowledged = false;
                if (actionError.find("Could not create the disk image.") != std::string::npos)
                    diskErrorUnacknowledged = false;
                if (actionError.find("Could not save the debugger export. The file may be incomplete.") != std::string::npos)
                    exportErrorUnacknowledged = false;
                if (actionError.find(printerError) != std::string::npos)
                    printerErrorUnacknowledged = false;
                if (actionError.find(tapeCreateError) != std::string::npos)
                    tapeCreateErrorUnacknowledged = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        }
        if (toggleFullscreen && !sdl_display::ToggleFullscreen(*window))
        { actionError = "Could not change fullscreen mode."; showActionError = true; }
        (void)updateAudio(running && soundEnabled, speedChanged, volumeChanged);
        int width = 0, height = 0;
        if (!SDL_GetRenderOutputSize(renderer.get(), &width, &height)) return 1;
        const auto scale = ImGui::GetIO().DisplayFramebufferScale;
        const auto destination = sdl_display::Viewport(static_cast<float>(width), static_cast<float>(height), scale.y,
            menuHeight + toolbarHeight, statusHeight);
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
        if (tape.TakeError()) { actionError = "Tape I/O failed and playback or recording was stopped."; showActionError = true; }
#ifdef UKNCBTL_TEST_HOOKS
        if (testStaticVideo)
        {
            if (staticVideoPhase == 2) paletteMode = (paletteMode + 1) % 3;
            if (staticVideoPhase == 3) displayMode = (displayMode + 1) % static_cast<int>(screen_scale::Modes().size());
            if (staticVideoPhase == 4) board.SetRAMByte(0, 0, board.GetRAMByte(0, 0) ^ 0xff);
        }
#endif
        const auto palette = video_palette::Get(static_cast<video_palette::Mode>(paletteMode));
        if (copyScreenText)
        {
#ifdef UKNCBTL_TEST_HOOKS
            if (testClipboardSupersede && !clipboardCopy.ImagePending()) return 1;
#endif
            const bool startedText = !clipboardCopy.TextPending() && startScreenTextCopy();
            if (!startedText) reportActionError(screenTextError);
#ifdef UKNCBTL_TEST_HOOKS
            if (testClipboardSupersede || testScreenTextExit)
            {
                if (!startedText || !clipboardCopy.TextPending()) return 1;
                requestedTestClipboardText = true;
                if (testScreenTextExit) done = true;
            }
#endif
        }
        const bool frameStarted = !done && running && scheduler.Due(started);
#ifdef UKNCBTL_TEST_HOOKS
        if (testAudioStartupOverlap && frameStarted && frames == 0 && (!audioOpen.Pending() || audio.IsOpen())) return 1;
        if (testDebuggerInspection && frameStarted && !requestedDebuggerSearch)
        {
            using enum preview_debugger::MemorySource;
            const auto before = pumpedBoardOperations;
            board.SetRAMByte(1, testMemoryAddress, 0x33);
            if (!captureComparison()) return 1;
            board.SetRAMByte(1, testMemoryAddress, 0xac);
            if (!debugger.MemoryChanged(ram1, debugProcessor, testMemoryAddress, true)) return 1;
            settings.memorySource = static_cast<int>(ram1);
            settings.memoryBytes = true;
            WriteOctalAddress(memoryAddress, testMemoryAddress - 1);
            if (!startMemorySearch(testMemoryAddress - 1, 0xac, preview_debugger::SearchDirection::forward) ||
                (testBoardOperations && pumpedBoardOperations - before != 2)) return 1;
            board.SetRAMByte(1, testMemoryAddress, 0x33);
            if (testDebuggerSearchDiscard)
                WriteOctalAddress(memoryAddress, testMemoryAddress + 1);
            requestedDebuggerSearch = true;
        }
#endif
        const bool screenshotConflict = screenshotPath && conflicts(*screenshotPath);
#ifdef UKNCBTL_TEST_HOOKS
        if (testScreenshotConflict && screenshotPath)
        {
            if (!screenshotConflict || screenshotSave.Pending()) return 1;
            verifiedScreenshotConflict = true;
        }
#endif
        if (frameStarted)
        {
            scheduler.FrameStarted(started, settings.speed, FRAMERATE);
            if (const auto key = keyDelivery.Next()) board.KeyboardEvent(key->scan, key->pressed);
            if (settings.mouse)
            {
                const auto sample = mouse.Next();
                board.MouseMove(sample.dx, sample.dy, sample.left, sample.right);
            }
        }
        const bool memoryChanged = video::UpdateMemorySnapshot(board, videoMemory);
        if (frameStarted && !frameWorker.Start()) return 1;

        const auto videoModes = std::pair{displayMode, paletteMode};
        const auto& dimensions = screen_scale::GetMode(displayMode);
        const bool prepareVideo = memoryChanged || preparedVideoModes != videoModes;
        if (prepareVideo)
        {
            displayPixels.resize(static_cast<std::size_t>(dimensions.width) * dimensions.height);
            if (!videoDecoder.RenderScaled(videoMemory, pixels, palette, displayMode, displayPixels,
                finalizeGui, screen_scale::RowOrder::topDown))
                return 1;
            preparedVideoModes = videoModes;
            screenNeedsUpload = true;
        }
        else finalizeGui();
#ifdef UKNCBTL_TEST_HOOKS
        // A reset queued after SDL's poll sentinel may be delivered on the following iteration.
        const bool staticVideoResetPending = testStaticVideo && staticVideoPhase == 5 &&
            !screenNeedsUpload && SDL_HasEvent(SDL_EVENT_RENDER_DEVICE_RESET);
        if (testStaticVideo && staticVideoPhase < 6)
        {
            const bool expectPreparation = staticVideoPhase != 1 && staticVideoPhase != 5;
            const bool expectUpload = staticVideoPhase != 1 && !staticVideoResetPending;
            if (frameStarted || prepareVideo != expectPreparation || screenNeedsUpload != expectUpload ||
                destination.w <= 0 || destination.h <= 0)
            {
                SDL_Log("Static video phase %u: frame started=%d, prepared video=%d, needs upload=%d, viewport=%gx%g",
                    staticVideoPhase, frameStarted, prepareVideo, screenNeedsUpload, destination.w, destination.h);
                return 1;
            }
        }
#endif
        if (copyScreenshot || screenshotPath)
        {
            if (copyScreenshot)
            {
#ifdef UKNCBTL_TEST_HOOKS
                if (testCopyScreenshot && !testClipboardExit && !testClipboardDuringFrame)
                    screenshot_clipboard::testing::HoldEncoding(true);
#endif
                if (clipboardCopy.ImagePending()) reportActionError("A screenshot is already being copied to the clipboard.");
                else if (!clipboardCopy.StartScaled(pixels, screenshotMode)) reportActionError(clipboardError);
#ifdef UKNCBTL_TEST_HOOKS
                if (testCopyScreenshot && !clipboardCopy.ImagePending()) return 1;
                if (testClipboardExit) done = true;
#endif
            }
            if (screenshotConflict)
            {
                if (showActionError) actionError += "\n";
                else actionError.clear();
                actionError += "The selected file is used by the emulator. Choose another file.";
                showActionError = true;
            }
            else if (screenshotPath)
            {
                const bool saveStarted = screenshotSave.StartScaled(*screenshotPath, pixels, screenshotMode);
                if (!saveStarted)
                {
                    if (showActionError) actionError += "\n";
                    else actionError.clear();
                    actionError += screenshotSave.Pending() ? "A screenshot is already being saved." : "Could not start saving the screenshot image.";
                    showActionError = true;
                }
            }
        }
#ifdef UKNCBTL_TEST_HOOKS
        // Exercise the same cleanup as a renderer error while a frame and an
        // optional screenshot are outstanding.
        if (testEarlyFrameExit && frameStarted) return 1;
        if (testAudioOverlap && frameStarted && !verifiedAudioOverlap)
        {
            if (!frameWorker.WaitFor(std::chrono::seconds(2)) || !audioSubmittedBeforePresent)
            { SDL_Log("Audio submission did not complete before presentation"); return 1; }
            verifiedAudioOverlap = true;
        }
#endif
        {
        UKNC_PROFILE_SCOPE("SDL render");
        if (!SDL_SetRenderDrawColor(renderer.get(), 0, 0, 0, 255) || !SDL_RenderClear(renderer.get())) return 1;
        if (destination.w > 0 && destination.h > 0)
        {
            if (screenNeedsUpload)
            {
                if (!screen.UploadTopDown(dimensions.width, dimensions.height, displayPixels))
                { SDL_Log("Screen upload failed: %s", SDL_GetError()); return 1; }
                screenNeedsUpload = false;
            }
            if (!screen.Draw(destination)) { SDL_Log("Screen draw failed: %s", SDL_GetError()); return 1; }
        }
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer.get());
        {
            UKNC_PROFILE_SCOPE("SDL present");
            if (!SDL_RenderPresent(renderer.get())) return 1;
        }
        }
#ifdef UKNCBTL_TEST_HOOKS
        if (testStaticVideo && staticVideoPhase < 6 && !staticVideoResetPending)
        {
            if (++staticVideoPhase == 5)
            {
                SDL_Event reset{};
                reset.type = SDL_EVENT_RENDER_DEVICE_RESET;
                if (!SDL_PushEvent(&reset)) return 1;
            }
            if (staticVideoPhase == 6) startMachine(started);
        }
#endif
        if (frameStarted)
        {
            UKNC_PROFILE_SCOPE("Frame worker wait");
            bool clipboardOk = true;
#ifdef UKNCBTL_TEST_HOOKS
            if (testSaveDuringFrame && frames == 0 && !fileOutputPending()) return 1;
            if (testScreenshotPath && !testScreenshotConflict && requestedTestScreenshot && !verifiedScreenshotReservation)
            {
                const auto path = PathFromUtf8(testScreenshotPath);
                if (!reservesPath(screenshotSave, path.parent_path() / "." / path.filename())) return 1;
                verifiedScreenshotReservation = true;
            }
#endif
            const auto outcome = sdl_display::WaitForWorker(frameWorker, boardWake, [&] {
                if (!done)
                {
                    completionWake.Clear();
                    collectFileOutputs();
                    if (clipboardOk) clipboardOk = collectClipboard(true);
                    if (collectMemorySearch())
                    {
                        backgroundCompletedDuringEvents = true;
#ifdef UKNCBTL_TEST_HOOKS
                        if (testDebuggerInspection && frames == 0)
                        {
                            const auto expected = testDebuggerSearchDiscard ? testMemoryAddress + 1 : testMemoryAddress;
                            verifiedDebuggerSearch = memoryError.empty() &&
                                preview_debugger::ParseWord(memoryAddress.data()) == expected;
                        }
#endif
                    }
                }
#ifdef UKNCBTL_TEST_HOOKS
                if (testSaveDuringFrame && frames == 0 && !fileOutputPending())
                    verifiedSaveDuringFrame = !screenshotErrorUnacknowledged && !snapshotErrorUnacknowledged &&
                        !diskErrorUnacknowledged && !exportErrorUnacknowledged;
                if (testSlowFrame && !pumpedDuringSlowFrame)
                {
                    SDL_Event event{};
                    event.type = testSlowEventType;
                    event.user.code = 1;
                    const bool firstQueued = SDL_PushEvent(&event);
                    event.user.code = 2;
                    pumpedDuringSlowFrame = firstQueued && SDL_PushEvent(&event);
                }
                if (testSlowFrame && pumpedDuringSlowFrame && (!testClipboardDuringFrame || verifiedTestClipboard) &&
                    (!testSaveDuringFrame || verifiedSaveDuringFrame) &&
                    (!testDebuggerInspection || verifiedDebuggerSearch)) releaseSlowFrame.store(true);
#endif
            });
            if (!clipboardOk) return 1;
            frameFailure.Observe(outcome);
            const bool completed = outcome == FrameWorker::Outcome::completed;
            if (!completed)
            {
                pauseMachine();
                showDebugger = true;
                if (outcome == FrameWorker::Outcome::failed)
                {
                    SDL_Log("Emulation frame failed; reset is required.");
                    reportActionError("Emulation stopped after a frame error. Reset the machine before running again.");
                }
            }
            if (!printerOkAfterFrame)
            {
                reportPrinterError();
#ifdef UKNCBTL_TEST_HOOKS
                if (testPrinterFailureExit) done = true;
#endif
            }
            if (completed)
            {
                if (const auto event = autoBoot.FrameCompleted())
                    queueKey(sdl_keyboard::Source::automatic, {event->scan, event->pressed});
                snapshots.FrameCompleted();
                if (smokeTest) ++frames;
#ifdef UKNCBTL_TEST_HOOKS
                if (testTraceFailure && verifiedTraceRecovery) ++framesAfterTraceRecovery;
                if (testCopyScreenshot && frames == 3)
                {
                    screenshot_clipboard::testing::HoldEncoding(false);
                    releaseScreenText.store(true);
                }
                if (testAudioOverlap && frames == 1)
                {
                    const auto before = pumpedBoardOperations;
                    if (!updateAudio(false, false, false) || audio.Active() ||
                        !updateAudio(true, false, true) || !audio.Active() || pumpedBoardOperations - before != 2)
                    { SDL_Log("Audio pause and resume did not return through the board worker"); return 1; }
                    testRemovedAudioDevice = audio.LogicalDevice();
                    if (!testRemovedAudioDevice) return 1;
                    SDL_Event removed{};
                    removed.type = SDL_EVENT_AUDIO_DEVICE_REMOVED;
                    removed.adevice.which = testRemovedAudioDevice;
                    if (!SDL_PushEvent(&removed)) return 1;
                }
#endif
            }
#ifdef UKNCBTL_TEST_HOOKS
            if ((testFrameFailure || testTraceRecordFailure) && outcome == FrameWorker::Outcome::failed)
            {
                if (!frameFailure.Failed() || running) return 1;
                if (testFrameAndPrinterFailure &&
                    (actionError.find("Printer output failed") == std::string::npos ||
                     actionError.find("Emulation stopped after a frame error") == std::string::npos)) return 1;
                startMachine(started);
                if (running || !frameFailure.Failed()) return 1;
                if (testTraceRecordFailure) preview_debugger::testing::ThrowOnTraceRecord(0);
                if (!resetMachine() || frameFailure.Failed()) return 1;
                startMachine(started);
                if (!running) return 1;
                verifiedFrameRecovery = true;
                if (testFrameAndPrinterFailure) printerErrorUnacknowledged = false;
                actionError.clear();
                showActionError = false;
            }
#endif
        }
        if (smokeTest && frames >= 3
#ifdef UKNCBTL_TEST_HOOKS
            && !testScreenshotFailureExit && !testSnapshotFailureExit && !testTraceFailureExit && !testDiskFailureExit && !testExportFailureExit && (!testScreenshotErrorMerge || verifiedScreenshotErrorMerge)
            && (!testTraceFailure || (verifiedTraceRecovery && framesAfterTraceRecovery > 0))
            && (!testSnapshotLoadPath || verifiedSnapshotLoad)
            && (!testSubtitlePath || verifiedSubtitleLoad)
            && (!testRomPath || verifiedRomLoad)
            && (!testTapePath || verifiedTapeOpen)
            && !testPrinterFailureExit
            && (!testCopyScreenshot || verifiedTestClipboard)
            && (!testDiskMountPath || verifiedDiskMount)
            && (!(testAudioOverlap || testAudioStartupOverlap) || !audioOpen.Pending())
            && (!testAudioStartupOverlap || !audioDeviceScan.Pending())
#endif
            )
        {
#ifdef UKNCBTL_TEST_HOOKS
            if ((testFrameFailure || testTraceRecordFailure) && !verifiedFrameRecovery) return 1;
            if (testScreenshotPath && !testScreenshotConflict && !verifiedScreenshotReservation) return 1;
            if (testScreenshotConflict && !verifiedScreenshotConflict) return 1;
            if (testDebuggerInspection && !verifiedDebuggerSearch) return 1;
            if (testSaveDuringFrame && !verifiedSaveDuringFrame) return 1;
            if (testSlowFrame && (!pumpedDuringSlowFrame || receivedSlowEvents != 2)) return 1;
            if (testAudioOverlap && (!verifiedAudioOverlap || !verifiedAudioReplacement))
            {
                SDL_Log("Audio overlap or replacement was not verified: submission=%d replacement=%d pending=%d lost=%d",
                    verifiedAudioOverlap, verifiedAudioReplacement, audioOpen.Pending(), audioDeviceLost);
                return 1;
            }
            if (testAudioStartupOverlap && (!verifiedAudioStartup || audioDevices.empty()))
            { SDL_Log("Startup audio or output enumeration did not complete during emulation"); return 1; }
            if (testSnapshotPath && !verifiedSnapshotReservation) return 1;
            if (testDiskPath && !verifiedDiskReservation) return 1;
            if (testExportPath && !verifiedExportReservation) return 1;
            if (testBoardOperations && testPrinterPath)
            {
                pauseMachine();
                if (!stopPrinter() || printer.Active() || pumpedBoardOperations != 3) return 1;
            }
            if (testBoardOperations && testTracePath)
            {
                pauseMachine();
                if (!clearTrace() || submitConsoleCommand("s") != preview_console::Action::none ||
                    console.Log().find("Stepped both processors.") == std::string::npos) return 1;
                stepMachine(false, started);
                if (frameFailure.Failed() || !stopTrace() || debugger.TracePath() || pumpedBoardOperations != 5) return 1;
            }
#endif
            done = true;
        }
        {
            UKNC_PROFILE_SCOPE("Frame pacing wait");
            if (!done && collectMemorySearch()) backgroundCompletedDuringEvents = true;
            if (!done && !backgroundCompletedDuringEvents)
                (void)sdl_display::WaitForEvents(scheduler.Delay(frame_pacing::Clock::now(), settings.speed, running));
        }
    }
    // The last frame has returned the board. Capture everything that still
    // needs the window or ImGui before handing the board to shutdown.
    if (settingsPath)
    {
        auto currentLayout = settings;
        if (captureUiLayout(currentLayout))
        {
            if (settings.uiLayout != currentLayout.uiLayout)
            { settings.uiLayout = std::move(currentLayout.uiLayout); settingsDirty = true; }
        }
        else SDL_Log("Could not save the tool window layout; existing layout retained.");
        const auto cpuBreakpoints = debugger.Breakpoints(preview_debugger::Processor::cpu).PermanentAddresses();
        const auto ppuBreakpoints = debugger.Breakpoints(preview_debugger::Processor::ppu).PermanentAddresses();
        const bool debuggerPpu = debugProcessor == preview_debugger::Processor::ppu;
        if (settings.cpuBreakpoints != cpuBreakpoints || settings.ppuBreakpoints != ppuBreakpoints ||
            settings.debuggerVisible != showDebugger || settings.consoleVisible != showConsole || settings.debuggerPpu != debuggerPpu)
        {
            settings.cpuBreakpoints = cpuBreakpoints;
            settings.ppuBreakpoints = ppuBreakpoints;
            settings.debuggerVisible = showDebugger;
            settings.consoleVisible = showConsole;
            settings.debuggerPpu = debuggerPpu;
            settingsDirty = true;
        }
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
    }

#ifdef UKNCBTL_TEST_HOOKS
    if (testTapeCreateExit && tape.Inserted()) return 1;
    const Uint32 shutdownTestEvent = testShutdownPump ? SDL_RegisterEvents(1) : 0;
    if (testShutdownPump && !shutdownTestEvent) return 1;
    std::atomic<bool> observedShutdownEvent{false};
#endif
    struct ShutdownResult
    {
        bool screenshotFailed = false;
        bool snapshotFailed = false;
        bool diskFailed = false;
        bool exportFailed = false;
        bool traceFailed = false;
        bool tapeFailed = false;
        bool printerFailed = false;
        std::optional<bool> tapeCreated;
        bool settingsFailed = false;
    };
    const auto finishShutdown = [&] {
#ifdef UKNCBTL_TEST_HOOKS
        if (testShutdownPump)
        {
            SDL_Event event{};
            event.type = shutdownTestEvent;
            if (!SDL_PushEvent(&event)) throw std::runtime_error("Could not queue the shutdown test event");
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (!observedShutdownEvent.load() && std::chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            if (!observedShutdownEvent.load()) throw std::runtime_error("Shutdown did not pump events");
        }
#endif
        ShutdownResult result;
        (void)audioOpen.Finish();
        if (audioDeviceScan.Pending()) (void)audioDeviceScan.Take();
        audio.Close();
        result.screenshotFailed = !screenshotSave.Finish().value_or(true);
        result.snapshotFailed = !snapshotSave.Finish().value_or(true);
        result.diskFailed = !diskCreate.Finish().value_or(true);
        result.exportFailed = !debuggerExport.Finish().value_or(true);
        debugger.StopTrace();
        result.traceFailed = debugger.TakeTraceError();
        if (auto pending = tapePrepare.Finish(); pending && pending->Mode() == preview_tape::FileMode::recording)
            result.tapeCreated = pending->Close();
        result.tapeFailed = !tape.Close();
        result.printerFailed = !printer.Stop();

        if (settingsPath && settingsDirty)
            result.settingsFailed = !preview_settings::Save(*settingsPath, settings);

        // Readers may finish after quit. Release their results without
        // applying state or installing media on a board that is closing.
        (void)snapshotLoad.Finish();
        (void)romLoad.Finish();
        (void)subtitleLoad.Finish();
        diskMount.Discard();
        board.DrainFloppyReadAhead();
        for (int slot = 0; slot < 2; ++slot)
            if (media.Attached(media_slots::Kind::hardDisk, slot))
                (void)media.Eject(media_slots::Kind::hardDisk, slot);
        return result;
    };

    ShutdownResult shutdown;
    background::Job<ShutdownResult> shutdownWork(notifyUi);
    std::optional<ClipboardCopy::Outcome> finalClipboardImage, finalClipboardText;
    try
    {
        try { shutdownWork.Start(finishShutdown); }
        catch (const std::system_error&) { shutdown = finishShutdown(); }

        const auto collectShutdownClipboard = [&] {
            while (const auto copied = clipboardCopy.Poll())
                (copied->kind == ClipboardCopy::Kind::image ? finalClipboardImage : finalClipboardText) = copied->outcome;
#ifdef UKNCBTL_TEST_HOOKS
            if (testShutdownPump && SDL_HasEvent(shutdownTestEvent)) observedShutdownEvent.store(true);
#endif
        };
        completionWake.Clear();
        collectShutdownClipboard();
        if ((shutdownWork.Pending() && !shutdownWork.Ready()) || clipboardCopy.Pending())
        {
            (void)SDL_SetWindowTitle(window.get(), "UKNCBTL - Finishing background work...");
            sdl_display::WaitForCompletion(completionWake, [&](auto timeout) {
                if (shutdownWork.Pending() && !shutdownWork.WaitFor(timeout)) return false;
                if (!clipboardCopy.Pending()) return true;
                if (timeout.count() != 0) std::this_thread::sleep_for(timeout);
                return false;
            }, collectShutdownClipboard);
        }
        if (shutdownWork.Pending()) shutdown = shutdownWork.Take();
    }
    catch (const std::exception& error)
    {
        SDL_Log("Could not finish shutdown: %s", error.what());
        return 1;
    }
#ifdef UKNCBTL_TEST_HOOKS
    if (testAudioOpenExit && (!verifiedAudioOpenExit || audioOpen.Pending() || audio.IsOpen())) return 1;
    if (testClipboardExit && (finalClipboardImage != ClipboardCopy::Outcome::copied || !imageOnClipboard())) return 1;
    if (testScreenTextExit && (finalClipboardText != ClipboardCopy::Outcome::copied || !screenTextOnClipboard())) return 1;
#endif
    if (finalClipboardImage == ClipboardCopy::Outcome::failed) SDL_Log("%s", clipboardError.data());
    if (finalClipboardText == ClipboardCopy::Outcome::failed) SDL_Log("%s", screenTextError.data());
    const bool reportScreenshotError = screenshotErrorUnacknowledged || shutdown.screenshotFailed;
#ifdef UKNCBTL_TEST_HOOKS
    if (testScreenshotFailureExit && !screenshotErrorUnacknowledged) return 1;
#endif
    if (reportScreenshotError)
    {
        SDL_Log("Could not save the screenshot image.");
#ifdef UKNCBTL_TEST_HOOKS
        if (!testScreenshotFailureExit)
#endif
        (void)SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Screenshot", "Could not save the screenshot image.", window.get());
    }
#ifdef UKNCBTL_TEST_HOOKS
    if (testSnapshotFailureExit && !snapshotErrorUnacknowledged) return 1;
#endif
    if (snapshotErrorUnacknowledged || shutdown.snapshotFailed)
    {
        SDL_Log("Could not save the state image.");
#ifdef UKNCBTL_TEST_HOOKS
        if (!testSnapshotFailureExit)
#endif
        (void)SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "State image", "Could not save the state image.", window.get());
    }
#ifdef UKNCBTL_TEST_HOOKS
    if (testDiskFailureExit && !diskErrorUnacknowledged) return 1;
#endif
    if (diskErrorUnacknowledged || shutdown.diskFailed)
    {
        SDL_Log("Could not create the disk image.");
#ifdef UKNCBTL_TEST_HOOKS
        if (!testDiskFailureExit)
#endif
        (void)SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Disk image", "Could not create the disk image.", window.get());
    }
#ifdef UKNCBTL_TEST_HOOKS
    if (testExportFailureExit && !exportErrorUnacknowledged) return 1;
#endif
    if (exportErrorUnacknowledged || shutdown.exportFailed)
    {
        SDL_Log("Could not save the debugger export. The file may be incomplete.");
#ifdef UKNCBTL_TEST_HOOKS
        if (!testExportFailureExit)
#endif
        (void)SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Debugger export", "Could not save the debugger export. The file may be incomplete.", window.get());
    }
#ifdef UKNCBTL_TEST_HOOKS
    if (testTraceFailureExit && !traceErrorUnacknowledged) return 1;
#endif
    if (traceErrorUnacknowledged || shutdown.traceFailed)
    {
        SDL_Log("Could not finish the trace output.");
#ifdef UKNCBTL_TEST_HOOKS
        if (!testTraceFailureExit)
#endif
        (void)SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Trace", "Could not finish the trace output.", window.get());
    }
#ifdef UKNCBTL_TEST_HOOKS
    if (testTapeCreateExit && (!shutdown.tapeCreated || *shutdown.tapeCreated == testTapeOpenFailure)) return 1;
#endif
    if (tapeCreateErrorUnacknowledged || !shutdown.tapeCreated.value_or(true))
    {
        SDL_Log("Could not finish creating the WAV file.");
#ifdef UKNCBTL_TEST_HOOKS
        if (!testTapePath)
#endif
            (void)SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Tape", "Could not finish creating the WAV file.", window.get());
    }
    if (shutdown.tapeFailed)
    {
        SDL_Log("Could not finish the WAV file.");
        (void)SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Tape", "Could not finish the WAV file.", window.get());
    }
    if (shutdown.printerFailed || printerErrorUnacknowledged)
    {
        SDL_Log("Could not finish the printer output file.");
#ifdef UKNCBTL_TEST_HOOKS
        if (!testPrinterFailureExit)
#endif
        (void)SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Printer", "Could not finish the printer output file.", window.get());
    }
    if (shutdown.settingsFailed)
    {
        SDL_Log("Could not save application settings.");
        (void)SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Settings", "Could not save application settings.", window.get());
    }
    return 0;
}
}

int main(int argc, char** argv)
{
    std::vector<std::string_view> arguments;
    for (int index = 1; index < argc; ++index) arguments.emplace_back(argv[index]);
    const auto options = preview_options::Parse(arguments);
    if (!options)
    {
        std::fputs(options.error().c_str(), stderr);
        std::fputs("\nUse --help for usage.\n", stderr);
        return 2;
    }
    if (options->help)
    {
        std::fputs("Usage: ukncbtl [--run|--paused] [--sound|--no-sound] [--fullscreen|--windowed]\n"
                    "  [--rom PATH] [--floppy 0..3 PATH] [--cartridge 1..2 PATH]\n"
                    "  [--floppy-read-only 0..3 PATH] [--hard-disk 1..2 PATH]\n"
                    "  [--hard-disk-read-only 1..2 PATH] [--tape PATH] [--state PATH]\n"
                    "  [--boot 1..7] [--settings PATH]\n", stdout);
        return 0;
    }
    return Run(*options);
}
