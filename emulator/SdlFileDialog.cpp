/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "SdlFileDialog.h"
#include <SDL3/SDL_dialog.h>
#include <utility>

bool sdl_file_dialog::State::Begin()
{
    const std::scoped_lock lock(mutex_);
    if (pending_ || result_) return false;
    pending_ = true;
    return true;
}

void sdl_file_dialog::State::Complete(Result result)
{
    const std::scoped_lock lock(mutex_);
    if (!pending_) return;
    result_ = std::move(result);
    pending_ = false;
}

std::optional<sdl_file_dialog::Result> sdl_file_dialog::State::Take()
{
    const std::scoped_lock lock(mutex_);
    auto result = std::move(result_);
    result_.reset();
    return result;
}

bool sdl_file_dialog::State::Pending() const
{
    const std::scoped_lock lock(mutex_);
    return pending_;
}

namespace
{
struct Request {
    std::shared_ptr<sdl_file_dialog::State> state;
    media_slots::Kind kind;
    int slot;
    sdl_file_dialog::Purpose purpose;
    std::uint16_t start = 0, end = 0;
};

void SDLCALL OnDialog(void* userdata, const char* const* files, int filter)
{
    const std::unique_ptr<Request> request(static_cast<Request*>(userdata));
    try
    {
        const auto status = !files ? sdl_file_dialog::Status::failed :
            !files[0] ? sdl_file_dialog::Status::canceled : sdl_file_dialog::Status::selected;
        request->state->Complete({request->kind, request->slot, status,
            status == sdl_file_dialog::Status::selected ? files[0] : "", request->purpose, filter, request->start, request->end});
    }
    catch (...)
    {
        // Never let an exception cross SDL's C callback boundary.
        try { request->state->Complete({request->kind, request->slot, sdl_file_dialog::Status::failed, {}, request->purpose, -1, request->start, request->end}); }
        catch (...) {}
    }
}
}

bool sdl_file_dialog::Open(const std::shared_ptr<State>& state, SDL_Window& window,
    media_slots::Kind kind, int slot)
{
    if (!state) return false;
    auto request = std::make_unique<Request>(state, kind, slot, Purpose::disk);
    if (!state->Begin()) return false;
    static constexpr SDL_DialogFileFilter filters[]{{"Disk images", "dsk;rtd;img;hdi"}, {"All files", "*"}};
    SDL_ShowOpenFileDialog(OnDialog, request.release(), &window, filters, 2, nullptr, false);
    return true;
}

bool sdl_file_dialog::SaveScreenshot(const std::shared_ptr<State>& state, SDL_Window& window)
{
    if (!state) return false;
    auto request = std::make_unique<Request>(state, media_slots::Kind::floppy, -1, Purpose::screenshot);
    if (!state->Begin()) return false;
    static constexpr SDL_DialogFileFilter filters[]{{"PNG image", "png"}, {"BMP image", "bmp"}};
    SDL_ShowSaveFileDialog(OnDialog, request.release(), &window, filters, 2, nullptr);
    return true;
}

bool sdl_file_dialog::OpenSnapshot(const std::shared_ptr<State>& state, SDL_Window& window)
{
    if (!state) return false;
    auto request = std::make_unique<Request>(state, media_slots::Kind::floppy, -1, Purpose::snapshotLoad);
    if (!state->Begin()) return false;
    static constexpr SDL_DialogFileFilter filters[]{{"UKNC state images", "uknc"}, {"All files", "*"}};
    SDL_ShowOpenFileDialog(OnDialog, request.release(), &window, filters, 2, nullptr, false);
    return true;
}

bool sdl_file_dialog::SaveSnapshot(const std::shared_ptr<State>& state, SDL_Window& window)
{
    if (!state) return false;
    auto request = std::make_unique<Request>(state, media_slots::Kind::floppy, -1, Purpose::snapshotSave);
    if (!state->Begin()) return false;
    static constexpr SDL_DialogFileFilter filters[]{{"UKNC state images", "uknc"}, {"All files", "*"}};
    SDL_ShowSaveFileDialog(OnDialog, request.release(), &window, filters, 2, nullptr);
    return true;
}

bool sdl_file_dialog::OpenRom(const std::shared_ptr<State>& state, SDL_Window& window, Purpose purpose, int slot)
{
    if (!state || (purpose != Purpose::systemRom && purpose != Purpose::cartridge)) return false;
    if (purpose == Purpose::cartridge && (slot < 0 || slot >= 2)) return false;
    auto request = std::make_unique<Request>(state, media_slots::Kind::floppy, slot, purpose);
    if (!state->Begin()) return false;
    static constexpr SDL_DialogFileFilter filters[]{{"ROM images", "bin;rom"}, {"All files", "*"}};
    SDL_ShowOpenFileDialog(OnDialog, request.release(), &window, filters, 2, nullptr, false);
    return true;
}

bool sdl_file_dialog::SaveDisk(const std::shared_ptr<State>& state, SDL_Window& window, int tracks)
{
    if (!state || (tracks != 40 && tracks != 80)) return false;
    auto request = std::make_unique<Request>(state, media_slots::Kind::floppy, tracks, Purpose::createDisk);
    if (!state->Begin()) return false;
    static constexpr SDL_DialogFileFilter filters[]{{"Floppy disk images", "dsk"}};
    SDL_ShowSaveFileDialog(OnDialog, request.release(), &window, filters, 1, nullptr);
    return true;
}

bool sdl_file_dialog::SavePrinter(const std::shared_ptr<State>& state, SDL_Window& window)
{
    if (!state) return false;
    auto request = std::make_unique<Request>(state, media_slots::Kind::floppy, -1, Purpose::printer);
    if (!state->Begin()) return false;
    static constexpr SDL_DialogFileFilter filters[]{{"Printer output", "txt;log"}, {"All files", "*"}};
    SDL_ShowSaveFileDialog(OnDialog, request.release(), &window, filters, 2, nullptr);
    return true;
}

bool sdl_file_dialog::OpenTape(const std::shared_ptr<State>& state, SDL_Window& window)
{
    if (!state) return false;
    auto request = std::make_unique<Request>(state, media_slots::Kind::floppy, -1, Purpose::tapeOpen);
    if (!state->Begin()) return false;
    static constexpr SDL_DialogFileFilter filters[]{{"WAV audio", "wav"}, {"All files", "*"}};
    SDL_ShowOpenFileDialog(OnDialog, request.release(), &window, filters, 2, nullptr, false);
    return true;
}

bool sdl_file_dialog::SaveTape(const std::shared_ptr<State>& state, SDL_Window& window)
{
    if (!state) return false;
    auto request = std::make_unique<Request>(state, media_slots::Kind::floppy, -1, Purpose::tapeCreate);
    if (!state->Begin()) return false;
    static constexpr SDL_DialogFileFilter filters[]{{"WAV audio", "wav"}};
    SDL_ShowSaveFileDialog(OnDialog, request.release(), &window, filters, 1, nullptr);
    return true;
}

bool sdl_file_dialog::OpenSubtitles(const std::shared_ptr<State>& state, SDL_Window& window)
{
    if (!state) return false;
    auto request = std::make_unique<Request>(state, media_slots::Kind::floppy, -1, Purpose::subtitles);
    if (!state->Begin()) return false;
    static constexpr SDL_DialogFileFilter filters[]{{"Disassembly listings", "lst;txt"}, {"All files", "*"}};
    SDL_ShowOpenFileDialog(OnDialog, request.release(), &window, filters, 2, nullptr, false);
    return true;
}

bool sdl_file_dialog::SaveMemoryDump(const std::shared_ptr<State>& state, SDL_Window& window, bool cpu,
    std::uint16_t start, std::uint16_t end)
{
    if (!state || end <= start) return false;
    auto request = std::make_unique<Request>(state, media_slots::Kind::floppy, cpu ? 1 : 0, Purpose::memoryDump, start, end);
    if (!state->Begin()) return false;
    static constexpr SDL_DialogFileFilter filters[]{{"Binary memory dump", "bin"}};
    SDL_ShowSaveFileDialog(OnDialog, request.release(), &window, filters, 1, nullptr);
    return true;
}

bool sdl_file_dialog::SaveDisplayList(const std::shared_ptr<State>& state, SDL_Window& window)
{
    if (!state) return false;
    auto request = std::make_unique<Request>(state, media_slots::Kind::floppy, -1, Purpose::displayListDump);
    if (!state->Begin()) return false;
    static constexpr SDL_DialogFileFilter filters[]{{"Text file", "txt"}};
    SDL_ShowSaveFileDialog(OnDialog, request.release(), &window, filters, 1, nullptr);
    return true;
}
