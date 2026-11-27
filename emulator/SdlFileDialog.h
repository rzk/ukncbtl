/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once

#include "BackgroundJob.h"
#include "MediaSlots.h"
#include "PreviewDebugger.h"
#include <SDL3/SDL_video.h>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

namespace sdl_file_dialog
{
enum class Status { selected, canceled, failed };
enum class Purpose { disk, screenshot, snapshotLoad, snapshotSave, systemRom, cartridge, createDisk, printer, tapeOpen, tapeCreate, subtitles, memoryDump, displayListDump, consoleLog, trace };
struct Result
{
    media_slots::Kind kind;
    int slot;
    Status status;
    std::string path; // UTF-8, provided by SDL.
    Purpose purpose = Purpose::disk;
    int filter = -1;
    std::uint16_t start = 0, end = 0;
    bool readOnly = false;
};

// Results may arrive on an SDL worker thread. The state can outlive its UI.
class State
{
public:
    explicit State(background::Notify notify = {}) : notify_(std::move(notify)) {}
    [[nodiscard]] bool Begin();
    void Complete(Result result);
    [[nodiscard]] std::optional<Result> Take();
    [[nodiscard]] bool Pending() const;
private:
    mutable std::mutex mutex_;
    bool pending_ = false;
    std::optional<Result> result_;
    const background::Notify notify_;
};

// A desktop drop may contain several files. Accept exactly one, without
// silently choosing one of several or retaining SDL-owned event memory.
class DropBatch
{
public:
    void Begin();
    void Add(const char* path);
    [[nodiscard]] std::optional<std::string> Finish();
    [[nodiscard]] bool Rejected() const { return rejected_; }
private:
    std::optional<std::string> path_;
    bool rejected_ = false;
};

// Call on the SDL main thread. At most one dialog may be outstanding.
[[nodiscard]] bool Open(const std::shared_ptr<State>& state, SDL_Window& window,
    media_slots::Kind kind, int slot, bool readOnly = false);
[[nodiscard]] bool SaveScreenshot(const std::shared_ptr<State>& state, SDL_Window& window);
[[nodiscard]] bool OpenSnapshot(const std::shared_ptr<State>& state, SDL_Window& window);
[[nodiscard]] bool SaveSnapshot(const std::shared_ptr<State>& state, SDL_Window& window);
[[nodiscard]] bool OpenRom(const std::shared_ptr<State>& state, SDL_Window& window, Purpose purpose, int slot = -1);
[[nodiscard]] bool SaveDisk(const std::shared_ptr<State>& state, SDL_Window& window, int tracks);
[[nodiscard]] bool SavePrinter(const std::shared_ptr<State>& state, SDL_Window& window);
[[nodiscard]] bool OpenTape(const std::shared_ptr<State>& state, SDL_Window& window);
[[nodiscard]] bool SaveTape(const std::shared_ptr<State>& state, SDL_Window& window);
[[nodiscard]] bool OpenSubtitles(const std::shared_ptr<State>& state, SDL_Window& window);
// Resolve MemorySource::selected to CPU or PPU before opening the async dialog.
[[nodiscard]] bool SaveMemoryDump(const std::shared_ptr<State>& state, SDL_Window& window, preview_debugger::MemorySource source,
    std::uint16_t start, std::uint16_t end);
[[nodiscard]] bool SaveDisplayList(const std::shared_ptr<State>& state, SDL_Window& window);
[[nodiscard]] bool SaveConsoleLog(const std::shared_ptr<State>& state, SDL_Window& window);
// 0..2 select text encoding; 3..5 also request saved media paths.
[[nodiscard]] bool SaveTrace(const std::shared_ptr<State>& state, SDL_Window& window, std::uint16_t mask);
}
