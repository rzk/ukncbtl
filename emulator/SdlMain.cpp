/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "MediaSlots.h"
#include "AutoBoot.h"
#include "DiskImageFile.h"
#include "DisplayListDump.h"
#include "MouseInput.h"
#include "FramePacing.h"
#include "PreviewRoms.h"
#include "PreviewPrinter.h"
#include "PreviewOptions.h"
#include "PreviewDebugger.h"
#include "PreviewConsole.h"
#include "PreviewSettings.h"
#include "PreviewMediaRestore.h"
#include "PreviewSnapshot.h"
#include "PreviewSprite.h"
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
#include <format>
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

int ConsoleHistoryCallback(ImGuiInputTextCallbackData* data)
{
    auto& console = *static_cast<preview_console::Console*>(data->UserData);
    const auto text = console.Navigate(data->EventKey == ImGuiKey_UpArrow ? -1 : 1);
    data->DeleteChars(0, data->BufTextLen);
    if (!text.empty()) data->InsertChars(0, text.data(), text.data() + text.size());
    return 0;
}

int Run(const preview_options::Options& options)
{
    const bool smokeTest = options.smokeTest;
    if (!SDL_Init(SDL_INIT_VIDEO)) { SDL_Log("SDL initialization failed: %s", SDL_GetError()); return 1; }
    SdlRuntime sdl;
    preview_settings::Settings settings;
    std::optional<std::filesystem::path> settingsPath;
    std::optional<std::filesystem::path> protectedSettingsPath;
    std::string startupError;
    bool settingsDirty = false;
    bool importedSettings = false;
    if (!smokeTest)
    {
        if (options.settingsFile) settingsPath = *options.settingsFile;
        else
        {
            const std::unique_ptr<char, decltype(&SDL_free)> directory(SDL_GetPrefPath("UKNCBTL", "SDL Preview"), SDL_free);
            if (directory)
            {
                const std::u8string utf8(reinterpret_cast<const char8_t*>(directory.get()));
                settingsPath = std::filesystem::path(utf8) / "settings.ini";
            }
        }
        if (settingsPath)
        {
            protectedSettingsPath = settingsPath;
            const auto loaded = preview_settings::Load(*settingsPath);
            if (loaded) settings = *loaded;
            else { settingsPath.reset(); startupError = "Could not load preview settings. Changes will not be saved."; }
        }
        else startupError = "Could not locate preview settings. Changes will not be saved.";
    }
    else { settings.keyboard = true; settings.spriteViewer = true; settings.tapeVisible = true; } // Exercise preview windows in the smoke test.
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
    if (!ui_font::Install()) { SDL_Log("Could not load the UI font."); return 1; }
    ImGui::GetIO().IniFilename = nullptr;
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
    gui.platform = ImGui_ImplSDL3_InitForSDLRenderer(window.get(), renderer.get());
    if (!gui.platform) { SDL_Log("ImGui SDL initialization failed"); return 1; }
    gui.renderer = ImGui_ImplSDLRenderer3_Init(renderer.get());
    if (!gui.renderer) { SDL_Log("ImGui renderer initialization failed"); return 1; }

    ProcessorRuntime processor;
    CMotherboard board;
    media_slots::Slots media(board);
    preview_roms::Roms roms(board);
    const auto isSettingsFile = [&](const std::filesystem::path& path) {
        return protectedSettingsPath && preview_settings::SameFile(path, *protectedSettingsPath);
    };
    if ((options.rom && isSettingsFile(*options.rom)) ||
        (options.tape && isSettingsFile(*options.tape)) ||
        std::ranges::any_of(options.floppies, [&](const auto& path) { return path && isSettingsFile(*path); }) ||
        std::ranges::any_of(options.cartridges, [&](const auto& path) { return path && isSettingsFile(*path); }) ||
        std::ranges::any_of(options.hardDisks, [&](const auto& path) { return path && isSettingsFile(*path); }))
    { SDL_Log("The settings file cannot be used as a media image."); return 1; }
    if (options.rom && !roms.LoadSystem(*options.rom))
    { SDL_Log("Could not load the requested system ROM."); return 1; }
    for (int slot = 0; slot < 2; ++slot)
        if (options.cartridges[slot] && !roms.Insert(slot, *options.cartridges[slot]))
        { SDL_Log("Could not load requested cartridge %d.", slot + 1); return 1; }
    for (int slot = 0; slot < 4; ++slot)
        if (options.floppies[slot] && !media.Attach(media_slots::Kind::floppy, slot, *options.floppies[slot], options.floppyReadOnly[slot]))
        { SDL_Log("Could not attach requested floppy %d.", slot); return 1; }
    auto restoreErrors = preview_media_restore::Restore(settings, options, roms, media,
        protectedSettingsPath.value_or(std::filesystem::path{}));
    for (int slot = 0; slot < 2; ++slot)
        if (options.hardDisks[slot] && !media.Attach(media_slots::Kind::hardDisk, slot, *options.hardDisks[slot], options.hardDiskReadOnly[slot]))
        { SDL_Log("Could not attach requested hard disk %d; check its ROM cartridge and image.", slot + 1); return 1; }
    preview_printer::Session printer(board);
    preview_tape::Session tape(board);
    const auto* startupTape = options.tape ? &*options.tape :
        (!smokeTest && settings.restoreMedia && !settings.tapePath.empty() ? &settings.tapePath : nullptr);
    if (startupTape && (isSettingsFile(*startupTape) || media.Conflicts(*startupTape) || roms.Conflicts(*startupTape) || !tape.Open(*startupTape)))
    {
        if (options.tape) { SDL_Log("Could not open requested tape; check its format and whether it is already in use."); return 1; }
        restoreErrors.emplace_back("Tape");
    }
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
    const auto fileDialog = std::make_shared<sdl_file_dialog::State>();
    std::string actionError = startupError;
    bool showActionError = !startupError.empty();
    const auto closeTape = [&] {
        const bool closed = tape.Close();
        commandTape = false;
        if (settings.restoreMedia) { settings.tapePath.clear(); settingsDirty = true; }
        if (!closed) { actionError = "Could not finish the WAV file."; showActionError = true; }
    };
    board.Reset();
    board.SetSoundAY(settings.soundAY);
    board.SetSoundCovox(settings.soundCovox);
    board.SetMouse(settings.mouse);
    board.SetNetStation(settings.networkStation);
    if (smokeTest) debugger.SetHistoryEnabled(true);
    preview_snapshot::Session snapshots(board);
    sdl_audio::Playback audio(board);
    const bool initialSound = options.sound.value_or(settings.sound);
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO) || !audio.Open(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, settings.speed, settings.volume, false))
    {
        SDL_Log("Audio unavailable: %s", SDL_GetError());
        if (!smokeTest && initialSound)
        { if (!actionError.empty()) actionError += "\n"; actionError += "Audio is unavailable. Use Audio > Output to retry."; showActionError = true; }
    }
    if (smokeTest && !audio.IsOpen()) return 1;
    bool soundEnabled = initialSound;
    std::vector<std::uint32_t> pixels(video::Width * video::Height);
    sdl_screen::Renderer screen(*renderer);
    sdl_sprite::Texture spriteTexture(*renderer);
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
        if (!media.FlushFloppy())
        { actionError = "Could not flush floppy changes; reset was canceled."; showActionError = true; return false; }
        releaseKeys();
        releaseMouse();
        debugger.Stop();
        board.Reset();
        debugger.ClearHistory();
        board.SetMouse(settings.mouse);
        printer.Synchronize();
        snapshots.ResetTracking();
        if (audio.IsOpen() && !audio.Reset())
        {
            SDL_Log("Audio reset failed: %s", SDL_GetError());
            audio.Close(); actionError = "Audio failed. Use Audio > Output to retry."; showActionError = true;
        }
        return true;
    };
    const auto conflicts = [&](const std::filesystem::path& path) {
        return isSettingsFile(path) || media.Conflicts(path) || roms.Conflicts(path) || printer.Conflicts(path) || tape.Conflicts(path) || debugger.TraceConflicts(path);
    };
    bool running = smokeTest || options.boot != 0 || options.run.value_or(settings.autostart && restoreErrors.empty());
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
    std::array<char, 16> dumpStart{'0', '0', '0', '0', '0', '0', 0};
    std::array<char, 16> dumpEnd{'1', '7', '7', '7', '7', '7', 0};
    int registerIndex = 0;
    std::array<char, 16> disasmAddress{};
    std::uint16_t disasmBase = 0;
    bool followPc = true;
    std::string disasmError;
    std::array<char, 16> spriteAddress{};
    std::snprintf(spriteAddress.data(), spriteAddress.size(), "%06o", static_cast<unsigned>(settings.spriteAddress));
    std::string spriteError;
    std::string debuggerError;
    std::string registerError, memoryError;
    bool openMemory = false;
    int screenshotMode = settings.screenshotMode;
    int displayMode = settings.displayMode;
    int paletteMode = settings.paletteMode;
    sdl_file_dialog::DropBatch dropBatch;
    std::optional<std::string> droppedFile;
    bool showDropChooser = false, dropReadOnly = false;
    if (smokeTest) { droppedFile = "smoke-test.dsk"; showDropChooser = true; }
    unsigned frames = 0;
    while (true)
    {
        if (done)
        {
            if (forceQuit || media.FlushFloppy()) break;
            (void)board.TakeFloppyFailure();
            done = false;
            pauseMachine();
            showFloppyQuit = true;
        }
        bool copyScreenshot = false;
        bool copyScreenText = false;
        std::vector<sdl_shortcuts::Action> shortcuts;
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
        if (done) continue;
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
                if (result->purpose == sdl_file_dialog::Purpose::legacySettings)
                {
                    const auto source = std::filesystem::path(path);
                    std::error_code error;
                    const bool same = settingsPath && std::filesystem::equivalent(source, *settingsPath, error);
                    auto current = settings;
                    const bool layoutCaptured = captureUiLayout(current);
                    current.cpuBreakpoints = debugger.Breakpoints(preview_debugger::Processor::cpu).PermanentAddresses();
                    current.ppuBreakpoints = debugger.Breakpoints(preview_debugger::Processor::ppu).PermanentAddresses();
                    current.debuggerVisible = showDebugger;
                    current.consoleVisible = showConsole;
                    current.debuggerPpu = debugProcessor == preview_debugger::Processor::ppu;
                    int width = 0, height = 0;
                    const auto flags = SDL_GetWindowFlags(window.get());
                    if (!(flags & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_MAXIMIZED | SDL_WINDOW_FULLSCREEN)) &&
                        SDL_GetWindowSize(window.get(), &width, &height) && preview_settings::ValidWindowSize(width, height))
                    { current.windowWidth = width; current.windowHeight = height; }
                    const auto imported = preview_settings::ImportLegacy(source,
                        static_cast<preview_settings::ImportEncoding>(result->slot % 3), current, result->slot >= 3);
                    if (!layoutCaptured || !settingsPath || same || conflicts(source) || !imported ||
                        !media.FlushFloppy() || !preview_settings::Save(*settingsPath, *imported))
                    { actionError = "Could not import settings. Check the source, encoding and settings file."; showActionError = true; }
                    else
                    {
                        importedSettings = true;
                        (void)SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Settings imported",
                            "Settings were imported. The emulator will close; start it again to apply them.", window.get());
                        done = true;
                    }
                }
                else if (result->purpose == sdl_file_dialog::Purpose::trace)
                {
                    auto target = std::filesystem::path(path);
                    if (!target.has_extension()) target += ".log";
                    if (conflicts(target) || !debugger.StartTrace(target, static_cast<std::uint16_t>(result->slot)))
                    { actionError = "Could not start the trace. The selected file may be in use or unavailable."; showActionError = true; }
                }
                else if (result->purpose == sdl_file_dialog::Purpose::memoryDump ||
                    result->purpose == sdl_file_dialog::Purpose::displayListDump ||
                    result->purpose == sdl_file_dialog::Purpose::consoleLog)
                {
                    auto target = std::filesystem::path(path);
                    const bool memory = result->purpose == sdl_file_dialog::Purpose::memoryDump;
                    const bool log = result->purpose == sdl_file_dialog::Purpose::consoleLog;
                    if (!target.has_extension()) target += memory ? ".bin" : ".txt";
                    if (conflicts(target))
                    { actionError = "The selected file is used by the emulator. Choose another file."; showActionError = true; }
                    else if (memory ? !debugger.SaveMemory(target, static_cast<preview_debugger::MemorySource>(result->slot),
                        debugProcessor, result->start, result->end) : log ? !console.Save(target) :
                        !display_list_dump::Save(target, board))
                    { actionError = "Could not save the debugger export. The file may be incomplete."; showActionError = true; }
                }
                else if (result->purpose == sdl_file_dialog::Purpose::tapeOpen ||
                    result->purpose == sdl_file_dialog::Purpose::tapeCreate)
                {
                    auto target = std::filesystem::path(path);
                    const bool creating = result->purpose == sdl_file_dialog::Purpose::tapeCreate;
                    if (creating && !target.has_extension()) target += ".wav";
                    if (conflicts(target))
                    { actionError = "The selected file is used by the emulator. Choose another file."; showActionError = true; }
                    else if (!(creating ? tape.Create(target) : tape.Open(target)))
                    { actionError = creating ? "Could not create the WAV file." : "Could not open the WAV file or its format is unsupported."; showActionError = true; }
                    else
                    {
                        commandTape = false;
                        if (settings.restoreMedia) { settings.tapePath = creating ? std::filesystem::path{} : target; settingsDirty = true; }
                    }
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
                    (isSettingsFile(std::filesystem::path(path)) || printer.Conflicts(std::filesystem::path(path)) || tape.Conflicts(std::filesystem::path(path)) ||
                        debugger.TraceConflicts(std::filesystem::path(path))))
                { actionError = "The selected file is used for emulator output. Choose another file."; showActionError = true; }
                else if (result->purpose == sdl_file_dialog::Purpose::screenshot)
                {
                    const auto format = result->filter == 1 ? screenshot_format::Format::bmp : screenshot_format::Format::png;
                    screenshotPath = screenshot_file::WithDefaultExtension(std::filesystem::path(path), format);
                }
                else if (result->purpose == sdl_file_dialog::Purpose::subtitles)
                {
                    if (!debugger.LoadSubtitles(std::filesystem::path(path)))
                    { actionError = "Could not load the UTF-16LE disassembly listing."; showActionError = true; }
                }
                else if (result->purpose == sdl_file_dialog::Purpose::snapshotLoad)
                {
                    if (!media.FlushFloppy())
                    { actionError = "Could not flush floppy changes; state load was canceled."; showActionError = true; }
                    else if (!snapshots.Load(std::filesystem::path(path), [&] {
                        releaseKeys();
                        releaseMouse();
                        debugger.Stop();
                        running = false;
                        if (audio.IsOpen())
                        {
                            if (!audio.SetActive(false) || !audio.Reset())
                            {
                                SDL_Log("Audio pause failed: %s", SDL_GetError());
                                audio.Close(); actionError = "Audio failed. Use Audio > Output to retry."; showActionError = true;
                            }
                        }
                    }))
                    {
                        actionError = "Could not load the state image.";
                        showActionError = true;
                    }
                    else { roms.MarkRestoredFromSnapshot(); printer.Synchronize(); debugger.ClearHistory(); }
                }
                else if (result->purpose == sdl_file_dialog::Purpose::snapshotSave)
                {
                    const auto target = preview_snapshot::WithDefaultExtension(std::filesystem::path(path));
                    if (conflicts(target))
                    {
                        actionError = "The selected file is used by the emulator. Choose another file.";
                        showActionError = true;
                    }
                    else if (!media.FlushFloppy())
                    {
                        actionError = "Could not flush floppy changes; state save was canceled.";
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
                    if (!media.FlushFloppy())
                    { actionError = "Could not flush floppy changes; ROM change was canceled."; showActionError = true; }
                    else if (roms.LoadSystem(std::filesystem::path(path)))
                    { commandSystem = false; if (settings.restoreMedia) { settings.systemRom = std::filesystem::path(path); settingsDirty = true; } if (resetMachine()) scheduler.Restart(started); }
                    else { actionError = "Could not load the system ROM image."; showActionError = true; }
                }
                else if (result->purpose == sdl_file_dialog::Purpose::cartridge)
                {
                    if (!roms.Insert(result->slot, std::filesystem::path(path)))
                    {
                        actionError = "Could not load the ROM cartridge image.";
                        showActionError = true;
                    }
                    else { commandCartridges[result->slot] = false; if (settings.restoreMedia) { settings.cartridges[result->slot] = std::filesystem::path(path); settingsDirty = true; } }
                }
                else if (result->kind == media_slots::Kind::hardDisk && !roms.Inserted(result->slot))
                {
                    actionError = "Insert the matching hard-disk ROM cartridge first.";
                    showActionError = true;
                }
                else
                {
                    const auto attached = media.Attach(result->kind, result->slot, std::filesystem::path(path), result->readOnly);
                    if (!attached)
                    {
                        actionError = attached.error() == media_slots::Error::already_mounted ?
                            "The image is already mounted; shared mounts must all be read-only." :
                            "Could not attach the disk image. Check the file and the selected drive.";
                        showActionError = true;
                    }
                    else
                    {
                        if (result->kind == media_slots::Kind::floppy)
                        { commandFloppies[result->slot] = false; if (settings.restoreMedia) { settings.floppies[result->slot] = std::filesystem::path(path); settings.floppyReadOnly[result->slot] = result->readOnly; } }
                        else
                        { commandHardDisks[result->slot] = false; if (settings.restoreMedia) { settings.hardDisks[result->slot] = std::filesystem::path(path); settings.hardDiskReadOnly[result->slot] = result->readOnly; } }
                        if (settings.restoreMedia) settingsDirty = true;
                    }
                }
            }
        }
        if (done) continue;
        if (appliedTheme != settings.uiTheme || appliedScale != settings.uiScale)
        {
            if (ui_appearance::Apply(settings.uiTheme, settings.uiScale))
            { appliedTheme = settings.uiTheme; appliedScale = settings.uiScale; }
        }
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
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
            case sdl_shortcuts::Action::step: debugger.Step(); showDebugger = true; break;
            case sdl_shortcuts::Action::stepOver:
                showDebugger = true; if (debugger.StepOver(debugProcessor)) startMachine(started); break;
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
                if (ImGui::BeginMenu("Import legacy settings and exit", settingsPath.has_value() && !fileDialog->Pending()))
                {
                    constexpr std::array labels{"UTF-8 or UTF-16LE...", "Windows-1251...", "Windows-1252..."};
                    for (int media = 0; media < 2; ++media)
                        if (ImGui::BeginMenu(media ? "Settings and media paths" : "Settings only"))
                        {
                            if (media) ImGui::TextDisabled("Media will be restored on the next launch.");
                            for (int index = 0; index < static_cast<int>(labels.size()); ++index)
                                if (ImGui::MenuItem(labels[index]) && !sdl_file_dialog::OpenLegacySettings(fileDialog, *window, index + media * 3))
                                { actionError = "Could not open the file dialog."; showActionError = true; }
                            ImGui::EndMenu();
                        }
                    ImGui::EndMenu();
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
                    if (ImGui::MenuItem("Load ROM and reset...", nullptr, false, !fileDialog->Pending()) &&
                        !sdl_file_dialog::OpenRom(fileDialog, *window, sdl_file_dialog::Purpose::systemRom))
                    { actionError = "Could not open the file dialog."; showActionError = true; }
                    if (ImGui::MenuItem("Use embedded ROM and reset", nullptr, false,
                        roms.Source() != preview_roms::SystemSource::embedded))
                    {
                        if (!media.FlushFloppy())
                        { actionError = "Could not flush floppy changes; ROM change was canceled."; showActionError = true; }
                        else { roms.UseEmbedded(); commandSystem = false; if (settings.restoreMedia) { settings.systemRom.clear(); settingsDirty = true; } if (resetMachine()) scheduler.Restart(started); }
                    }
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
                        if (!audio.IsOpen()) ImGui::TextDisabled("Audio output unavailable");
                        const auto chooseDevice = [&](SDL_AudioDeviceID device) {
                            if (!(SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) && !SDL_InitSubSystem(SDL_INIT_AUDIO))
                            { actionError = "Could not initialize audio."; showActionError = true; }
                            else if (!audio.Open(device, settings.speed, settings.volume, running && soundEnabled))
                            { actionError = "Could not open the selected audio device."; showActionError = true; }
                        };
                        if (ImGui::MenuItem("System default", nullptr,
                            audio.IsOpen() && audio.Device() == SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK))
                            chooseDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK);
                        if (SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO)
                        {
                            const auto devices = sdl_audio::PlaybackDevices();
                            for (const auto& device : devices)
                            {
                                ImGui::PushID(static_cast<int>(device.id));
                                if (ImGui::MenuItem(device.name.c_str(), nullptr, audio.IsOpen() && audio.Device() == device.id))
                                    chooseDevice(device.id);
                                ImGui::PopID();
                            }
                        }
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
                    if (ImGui::MenuItem("Clear trace") && !debugger.ClearTrace())
                    { actionError = "Could not clear the trace."; showActionError = true; }
                    if (ImGui::MenuItem("Stop trace")) debugger.StopTrace();
                }
                else if (ImGui::MenuItem("Start trace...", nullptr, false, !fileDialog->Pending()) &&
                    !sdl_file_dialog::SaveTrace(fileDialog, *window, settings.traceMask))
                { actionError = "Could not open the file dialog."; showActionError = true; }
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
                        if (ImGui::MenuItem("Eject") && roms.Eject(slot))
                        { commandCartridges[slot] = false; if (settings.restoreMedia) { settings.cartridges[slot].clear(); settingsDirty = true; } }
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
                if (ImGui::MenuItem("Flush floppy changes") && !media.FlushFloppy())
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
                        if (ImGui::MenuItem("Eject"))
                        {
                            if (!media.Eject(kind, slot))
                            { actionError = "Could not eject the disk image."; showActionError = true; }
                            else if (kind == media_slots::Kind::floppy)
                            { commandFloppies[slot] = false; if (settings.restoreMedia) { settings.floppies[slot].clear(); settings.floppyReadOnly[slot] = false; settingsDirty = true; } }
                            else
                            { commandHardDisks[slot] = false; if (settings.restoreMedia) { settings.hardDisks[slot].clear(); settings.hardDiskReadOnly[slot] = false; settingsDirty = true; } }
                        }
                    }
                    else for (const bool readOnly : {false, true})
                    {
                        if (ImGui::MenuItem(readOnly ? "Attach read-only..." : "Attach read/write...", nullptr, false, !fileDialog->Pending()))
                        {
                            if (kind == media_slots::Kind::hardDisk && !roms.Inserted(slot))
                            { actionError = "Insert the matching hard-disk ROM cartridge first."; showActionError = true; }
                            else if (!sdl_file_dialog::Open(fileDialog, *window, kind, slot, readOnly))
                            { actionError = "Could not open the file dialog."; showActionError = true; }
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
                    {
                        if (tape.Playing()) tape.Stop();
                        else (void)tape.Play();
                    }
                    if (ImGui::MenuItem("Rewind") && !tape.Rewind())
                    { actionError = "Could not rewind the WAV file."; showActionError = true; }
                    if (ImGui::MenuItem("Eject")) closeTape();
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
        const float barHeight = ImGui::GetFrameHeight() + 2 * ImGui::GetStyle().WindowPadding.y;
        const float toolbarHeight = settings.toolbar ? barHeight : 0.0f;
        const float statusHeight = settings.statusBar ? barHeight : 0.0f;
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
                    !audio.IsOpen() ? "Audio unavailable" : soundEnabled ? "Sound on" : "Sound off");
            }
            ImGui::End();
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
                        if (!tape.Seek(at)) { actionError = "Could not seek in the WAV file."; showActionError = true; }
                    ImGui::EndDisabled();
                    if (tape.Recording()) ImGui::TextDisabled("Seeking is unavailable for recordings.");
                    else if (tape.Playing()) ImGui::TextDisabled("Stop playback to seek.");
                    std::array<float, 256> graph{};
                    const auto waveform = tape.Waveform();
                    std::ranges::transform(waveform, graph.begin(), [](std::uint8_t sample) { return static_cast<float>(sample); });
                    ImGui::PlotLines("##tape-signal", graph.data(), static_cast<int>(graph.size()), 0, "Signal", 0.0f, 255.0f, ImVec2(-1, 60));
                    if (ImGui::Button(tape.Playing() ? "Stop" : tape.Recording() ? "Record" : "Play"))
                    { if (tape.Playing()) tape.Stop(); else (void)tape.Play(); }
                    ImGui::SameLine();
                    if (ImGui::Button("Rewind") && !tape.Rewind())
                    { actionError = "Could not rewind the WAV file."; showActionError = true; }
                    ImGui::SameLine();
                    if (ImGui::Button("Eject")) closeTape();
                }
                else
                {
                    ImGui::TextUnformatted("No tape inserted.");
                    ImGui::BeginDisabled(fileDialog->Pending());
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
                            if (!SDL_SetClipboardText(text.c_str()))
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
                            if (!SDL_SetClipboardText(text.c_str()))
                            { actionError = "Could not copy the debugger address to the clipboard."; showActionError = true; }
                        }
                    }
                };
                const auto goMemory = [&](std::uint16_t address) {
                    std::snprintf(memoryAddress.data(), memoryAddress.size(), "%06o", static_cast<unsigned>(address));
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
                ImGui::BeginDisabled(running);
                if (ImGui::Button("Step")) debugger.Step();
                ImGui::SameLine();
                if (ImGui::Button("Step over") && debugger.StepOver(debugProcessor)) startMachine(started);
                ImGui::EndDisabled();
                ImGui::TextDisabled("Step advances both processors, as in the legacy debugger.");
                ImGui::BeginDisabled(running);
                if (ImGui::Button("Capture comparison baseline")) debugger.CaptureComparison();
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
                ImGui::BeginDisabled(fileDialog->Pending());
                if (ImGui::Button("Load subtitles..."))
                    if (!sdl_file_dialog::OpenSubtitles(fileDialog, *window))
                    { actionError = "Could not open the file dialog."; showActionError = true; }
                ImGui::EndDisabled();
                if (const auto* path = debugger.SubtitlePath())
                {
                    const auto text = path->u8string();
                    ImGui::TextUnformatted(reinterpret_cast<const char*>(text.c_str()));
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Unload subtitles")) debugger.UnloadSubtitles();
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
                        std::snprintf(memoryAddress.data(), memoryAddress.size(), "%06o", static_cast<unsigned>(addressProcessor.GetPC()));
                    ImGui::SameLine();
                    if (ImGui::Button("Use SP"))
                        std::snprintf(memoryAddress.data(), memoryAddress.size(), "%06o", static_cast<unsigned>(addressProcessor.GetSP()));
                    const auto oldBase = settings.memoryBytes ? preview_debugger::ParseWord(memoryAddress.data()) :
                        preview_debugger::ParseAddress(memoryAddress.data());
                    ImGui::SameLine();
                    ImGui::BeginDisabled(!oldBase);
                    if (ImGui::Button("Previous page") && oldBase)
                        std::snprintf(memoryAddress.data(), memoryAddress.size(), "%06o", static_cast<unsigned>(static_cast<std::uint16_t>(*oldBase - 64)));
                    ImGui::SameLine();
                    if (ImGui::Button("Next page") && oldBase)
                        std::snprintf(memoryAddress.data(), memoryAddress.size(), "%06o", static_cast<unsigned>(static_cast<std::uint16_t>(*oldBase + 64)));
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
                    const bool findPrevious = ImGui::Button("Find previous");
                    ImGui::SameLine();
                    const bool findNext = ImGui::Button("Find next");
                    if (findPrevious || findNext)
                    {
                        const auto value = preview_debugger::ParseMemoryValue(memorySearchValue.data(), settings.memoryHex, settings.memoryBytes);
                        if (!base || !value) memoryError = "Enter a valid address and search value.";
                        else if (const auto found = debugger.FindMemory(memorySource, debugProcessor, *base, *value,
                            settings.memoryBytes, findNext ? preview_debugger::SearchDirection::forward : preview_debugger::SearchDirection::backward))
                        {
                            std::snprintf(memoryAddress.data(), memoryAddress.size(), "%06o", static_cast<unsigned>(*found));
                            memoryError.clear();
                        }
                        else memoryError = "Value not found in accessible memory.";
                    }
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
                    std::snprintf(spriteAddress.data(), spriteAddress.size(), "%06o", static_cast<unsigned>(settings.spriteAddress));
                    spriteError.clear(); settingsDirty = true;
                };
                if (ImGui::Button("Previous page")) movePage(-1);
                ImGui::SameLine();
                if (ImGui::Button("Next page")) movePage(1);
                ImGui::TextDisabled("CPU memory, current CPU mode; page: %d bytes", page);
                const auto image = preview_sprite::Capture(board, settings.spriteAddress, settings.spriteWidth, settings.spriteMode);
                if (!image || !spriteTexture.Update(*image))
                {
                    actionError = "Could not prepare the sprite image."; showActionError = true;
                    settings.spriteViewer = false; settingsDirty = true;
                }
                else
                {
                    const auto id = static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(spriteTexture.Get()));
                    ImGui::Image(ImTextureRef(id), ImVec2(sprite::Width * 2.0f, sprite::Height * 2.0f));
                }
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
                if (ImGui::InputText("##console-input", consoleInput.data(), consoleInput.size(),
                    ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackHistory, ConsoleHistoryCallback, &console))
                {
                    const auto action = console.Submit(consoleInput.data(), debugger, debugProcessor, running);
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
        if (debugger.TakeTraceError())
        {
            if (showActionError && !actionError.empty()) actionError += "\n";
            else actionError.clear();
            actionError += "Trace output failed and tracing was stopped."; showActionError = true;
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
                        !media.Attached(media_slots::Kind::floppy, slot));
                }
                for (int slot = 0; slot < 2; ++slot)
                {
                    if (slot) ImGui::SameLine();
                    const auto label = std::format("Hard disk {}", slot + 1);
                    choose(label.c_str(), sdl_file_dialog::Purpose::disk, media_slots::Kind::hardDisk, slot,
                        roms.Inserted(slot) && !media.Attached(media_slots::Kind::hardDisk, slot));
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
                choose("Open tape", sdl_file_dialog::Purpose::tapeOpen, media_slots::Kind::floppy, -1, !tape.Inserted());
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
                if (media.FlushFloppy()) { (void)board.TakeFloppyFailure(); done = true; ImGui::CloseCurrentPopup(); }
                else (void)board.TakeFloppyFailure();
            }
            ImGui::SameLine();
            if (ImGui::Button("Exit anyway")) { forceQuit = true; done = true; ImGui::CloseCurrentPopup(); }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
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
        if (audio.IsOpen() && (speedChanged || volumeChanged) &&
            ((speedChanged && !audio.Reset()) || !audio.Configure(settings.speed, settings.volume)))
        {
            SDL_Log("Audio configuration failed: %s", SDL_GetError());
            audio.Close(); actionError = "Audio failed. Use Audio > Output to retry."; showActionError = true;
        }
        const bool shouldPlay = running && soundEnabled;
        if (audio.IsOpen() && shouldPlay != audio.Active())
        {
            if (!audio.SetActive(shouldPlay))
            {
                SDL_Log("Audio device failed: %s", SDL_GetError());
                audio.Close(); actionError = "Audio failed. Use Audio > Output to retry."; showActionError = true;
            }
        }
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
        if (!done && running && scheduler.Due(started))
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
                if (audio.IsOpen() && audio.Active() && !audio.SubmitFrame())
                {
                    SDL_Log("Audio submission failed: %s", SDL_GetError());
                    audio.Close(); actionError = "Audio failed. Use Audio > Output to retry."; showActionError = true;
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
        if (destination.w > 0 && destination.h > 0 && !screen.Draw(*image, destination)) { SDL_Log("Screen draw failed: %s", SDL_GetError()); return 1; }
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer.get());
        if (!SDL_RenderPresent(renderer.get())) return 1;
        if (smokeTest && frames >= 3) done = true;
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
    if (settingsPath && !importedSettings)
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
                    "  [--floppy-read-only 0..3 PATH] [--hard-disk 1..2 PATH]\n"
                    "  [--hard-disk-read-only 1..2 PATH] [--tape PATH] [--boot 1..7] [--settings PATH]\n");
        return 0;
    }
    return Run(*options);
}
