/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "SdlMediaDialog.h"
#include <SDL3/SDL_dialog.h>
#include <utility>

bool sdl_media_dialog::State::Begin()
{
    const std::scoped_lock lock(mutex_);
    if (pending_ || result_) return false;
    pending_ = true;
    return true;
}

void sdl_media_dialog::State::Complete(Result result)
{
    const std::scoped_lock lock(mutex_);
    if (!pending_) return;
    result_ = std::move(result);
    pending_ = false;
}

std::optional<sdl_media_dialog::Result> sdl_media_dialog::State::Take()
{
    const std::scoped_lock lock(mutex_);
    auto result = std::move(result_);
    result_.reset();
    return result;
}

bool sdl_media_dialog::State::Pending() const
{
    const std::scoped_lock lock(mutex_);
    return pending_;
}

namespace
{
struct Request { std::shared_ptr<sdl_media_dialog::State> state; media_slots::Kind kind; int slot; };

void SDLCALL OnDialog(void* userdata, const char* const* files, int)
{
    const std::unique_ptr<Request> request(static_cast<Request*>(userdata));
    try
    {
        const auto status = !files ? sdl_media_dialog::Status::failed :
            !files[0] ? sdl_media_dialog::Status::canceled : sdl_media_dialog::Status::selected;
        request->state->Complete({request->kind, request->slot, status,
            status == sdl_media_dialog::Status::selected ? files[0] : ""});
    }
    catch (...)
    {
        // Never let an exception cross SDL's C callback boundary.
        try { request->state->Complete({request->kind, request->slot, sdl_media_dialog::Status::failed, {}}); }
        catch (...) {}
    }
}
}

bool sdl_media_dialog::Open(const std::shared_ptr<State>& state, SDL_Window& window,
    media_slots::Kind kind, int slot)
{
    if (!state) return false;
    auto request = std::make_unique<Request>(state, kind, slot);
    if (!state->Begin()) return false;
    static constexpr SDL_DialogFileFilter filters[]{{"Disk images", "dsk;rtd;img;hdi"}, {"All files", "*"}};
    SDL_ShowOpenFileDialog(OnDialog, request.release(), &window, filters, 2, nullptr, false);
    return true;
}
