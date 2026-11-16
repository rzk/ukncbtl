/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once

#include "MediaSlots.h"
#include <SDL3/SDL_video.h>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

namespace sdl_media_dialog
{
enum class Status { selected, canceled, failed };
struct Result
{
    media_slots::Kind kind;
    int slot;
    Status status;
    std::string path; // UTF-8, provided by SDL.
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
}
