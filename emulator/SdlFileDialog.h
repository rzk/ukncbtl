/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once

#include "MediaSlots.h"
#include <SDL3/SDL_video.h>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

namespace sdl_file_dialog
{
enum class Status { selected, canceled, failed };
enum class Purpose { disk, screenshot, snapshotLoad, snapshotSave, systemRom, cartridge, createDisk, printer, tapeOpen, tapeCreate };
struct Result
{
    media_slots::Kind kind;
    int slot;
    Status status;
    std::string path; // UTF-8, provided by SDL.
    Purpose purpose = Purpose::disk;
    int filter = -1;
};

// Results may arrive on an SDL worker thread. The state can outlive its UI.
class State
{
public:
    [[nodiscard]] bool Begin();
    void Complete(Result result);
    [[nodiscard]] std::optional<Result> Take();
    [[nodiscard]] bool Pending() const;
private:
    mutable std::mutex mutex_;
    bool pending_ = false;
    std::optional<Result> result_;
};

// Call on the SDL main thread. At most one dialog may be outstanding.
[[nodiscard]] bool Open(const std::shared_ptr<State>& state, SDL_Window& window,
    media_slots::Kind kind, int slot);
[[nodiscard]] bool SaveScreenshot(const std::shared_ptr<State>& state, SDL_Window& window);
[[nodiscard]] bool OpenSnapshot(const std::shared_ptr<State>& state, SDL_Window& window);
[[nodiscard]] bool SaveSnapshot(const std::shared_ptr<State>& state, SDL_Window& window);
[[nodiscard]] bool OpenRom(const std::shared_ptr<State>& state, SDL_Window& window, Purpose purpose, int slot = -1);
[[nodiscard]] bool SaveDisk(const std::shared_ptr<State>& state, SDL_Window& window, int tracks);
[[nodiscard]] bool SavePrinter(const std::shared_ptr<State>& state, SDL_Window& window);
[[nodiscard]] bool OpenTape(const std::shared_ptr<State>& state, SDL_Window& window);
[[nodiscard]] bool SaveTape(const std::shared_ptr<State>& state, SDL_Window& window);
}
