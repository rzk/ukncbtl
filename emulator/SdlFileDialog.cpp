/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "SdlFileDialog.h"
#include <SDL3/SDL_dialog.h>
#include <array>
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
    {
        const std::scoped_lock lock(mutex_);
        if (!pending_) return;
        result_ = std::move(result);
        pending_ = false;
    }
    background::Signal(notify_);
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

void sdl_file_dialog::DropBatch::Begin()
{
    path_.reset();
    rejected_ = false;
}

void sdl_file_dialog::DropBatch::Add(const char* path)
{
    if (rejected_) return;
    if (!path || !*path || path_) { path_.reset(); rejected_ = true; return; }
    path_ = path;
}

std::optional<std::string> sdl_file_dialog::DropBatch::Finish()
{
    auto result = std::move(path_);
    path_.reset();
    return result;
}

namespace
{
struct Request {
    std::shared_ptr<sdl_file_dialog::State> state;
    media_slots::Kind kind;
    int slot;
    sdl_file_dialog::Purpose purpose;
    std::uint16_t start = 0, end = 0;
    bool readOnly = false;
};

void SDLCALL OnDialog(void* userdata, const char* const* files, int filter)
{
    const std::unique_ptr<Request> request(static_cast<Request*>(userdata));
    try
    {
        const auto status = !files ? sdl_file_dialog::Status::failed :
            !files[0] ? sdl_file_dialog::Status::canceled : sdl_file_dialog::Status::selected;
        request->state->Complete({request->kind, request->slot, status,
            status == sdl_file_dialog::Status::selected ? files[0] : "", request->purpose, filter, request->start, request->end, request->readOnly});
    }
    catch (...)
    {
        // Never let an exception cross SDL's C callback boundary.
        try { request->state->Complete({request->kind, request->slot, sdl_file_dialog::Status::failed, {}, request->purpose, -1, request->start, request->end, request->readOnly}); }
        catch (...) {}
    }
}
}

bool sdl_file_dialog::Open(const std::shared_ptr<State>& state, SDL_Window& window,
    media_slots::Kind kind, int slot, bool readOnly)
{
    if (!state) return false;
    auto request = std::make_unique<Request>(state, kind, slot, Purpose::disk, 0, 0, readOnly);
    if (!state->Begin()) return false;
    static constexpr auto filters = std::to_array<SDL_DialogFileFilter>({{"Disk images", "dsk;rtd;img;hdi"}, {"All files", "*"}});
    SDL_ShowOpenFileDialog(OnDialog, request.release(), &window, filters.data(), static_cast<int>(filters.size()), nullptr, false);
    return true;
}

bool sdl_file_dialog::SaveScreenshot(const std::shared_ptr<State>& state, SDL_Window& window)
{
    if (!state) return false;
    auto request = std::make_unique<Request>(state, media_slots::Kind::floppy, -1, Purpose::screenshot);
    if (!state->Begin()) return false;
    static constexpr auto filters = std::to_array<SDL_DialogFileFilter>({{"PNG image", "png"}, {"BMP image", "bmp"}});
    SDL_ShowSaveFileDialog(OnDialog, request.release(), &window, filters.data(), static_cast<int>(filters.size()), nullptr);
    return true;
}

bool sdl_file_dialog::OpenSnapshot(const std::shared_ptr<State>& state, SDL_Window& window)
{
    if (!state) return false;
    auto request = std::make_unique<Request>(state, media_slots::Kind::floppy, -1, Purpose::snapshotLoad);
    if (!state->Begin()) return false;
    static constexpr auto filters = std::to_array<SDL_DialogFileFilter>({{"UKNC state images", "uknc"}, {"All files", "*"}});
    SDL_ShowOpenFileDialog(OnDialog, request.release(), &window, filters.data(), static_cast<int>(filters.size()), nullptr, false);
    return true;
}

bool sdl_file_dialog::SaveSnapshot(const std::shared_ptr<State>& state, SDL_Window& window)
{
    if (!state) return false;
    auto request = std::make_unique<Request>(state, media_slots::Kind::floppy, -1, Purpose::snapshotSave);
    if (!state->Begin()) return false;
    static constexpr auto filters = std::to_array<SDL_DialogFileFilter>({{"UKNC state images", "uknc"}, {"All files", "*"}});
    SDL_ShowSaveFileDialog(OnDialog, request.release(), &window, filters.data(), static_cast<int>(filters.size()), nullptr);
    return true;
}

bool sdl_file_dialog::OpenRom(const std::shared_ptr<State>& state, SDL_Window& window, Purpose purpose, int slot)
{
    if (!state || (purpose != Purpose::systemRom && purpose != Purpose::cartridge)) return false;
    if (purpose == Purpose::cartridge && (slot < 0 || slot >= 2)) return false;
    auto request = std::make_unique<Request>(state, media_slots::Kind::floppy, slot, purpose);
    if (!state->Begin()) return false;
    static constexpr auto filters = std::to_array<SDL_DialogFileFilter>({{"ROM images", "bin;rom"}, {"All files", "*"}});
    SDL_ShowOpenFileDialog(OnDialog, request.release(), &window, filters.data(), static_cast<int>(filters.size()), nullptr, false);
    return true;
}

bool sdl_file_dialog::SaveDisk(const std::shared_ptr<State>& state, SDL_Window& window, int tracks)
{
    if (!state || (tracks != 40 && tracks != 80)) return false;
    auto request = std::make_unique<Request>(state, media_slots::Kind::floppy, tracks, Purpose::createDisk);
    if (!state->Begin()) return false;
    static constexpr auto filters = std::to_array<SDL_DialogFileFilter>({{"Floppy disk images", "dsk"}});
    SDL_ShowSaveFileDialog(OnDialog, request.release(), &window, filters.data(), static_cast<int>(filters.size()), nullptr);
    return true;
}

bool sdl_file_dialog::SavePrinter(const std::shared_ptr<State>& state, SDL_Window& window)
{
    if (!state) return false;
    auto request = std::make_unique<Request>(state, media_slots::Kind::floppy, -1, Purpose::printer);
    if (!state->Begin()) return false;
    static constexpr auto filters = std::to_array<SDL_DialogFileFilter>({{"Printer output", "txt;log"}, {"All files", "*"}});
    SDL_ShowSaveFileDialog(OnDialog, request.release(), &window, filters.data(), static_cast<int>(filters.size()), nullptr);
    return true;
}

bool sdl_file_dialog::OpenTape(const std::shared_ptr<State>& state, SDL_Window& window)
{
    if (!state) return false;
    auto request = std::make_unique<Request>(state, media_slots::Kind::floppy, -1, Purpose::tapeOpen);
    if (!state->Begin()) return false;
    static constexpr auto filters = std::to_array<SDL_DialogFileFilter>({{"WAV audio", "wav"}, {"All files", "*"}});
    SDL_ShowOpenFileDialog(OnDialog, request.release(), &window, filters.data(), static_cast<int>(filters.size()), nullptr, false);
    return true;
}

bool sdl_file_dialog::SaveTape(const std::shared_ptr<State>& state, SDL_Window& window)
{
    if (!state) return false;
    auto request = std::make_unique<Request>(state, media_slots::Kind::floppy, -1, Purpose::tapeCreate);
    if (!state->Begin()) return false;
    static constexpr auto filters = std::to_array<SDL_DialogFileFilter>({{"WAV audio", "wav"}});
    SDL_ShowSaveFileDialog(OnDialog, request.release(), &window, filters.data(), static_cast<int>(filters.size()), nullptr);
    return true;
}

bool sdl_file_dialog::OpenSubtitles(const std::shared_ptr<State>& state, SDL_Window& window)
{
    if (!state) return false;
    auto request = std::make_unique<Request>(state, media_slots::Kind::floppy, -1, Purpose::subtitles);
    if (!state->Begin()) return false;
    static constexpr auto filters = std::to_array<SDL_DialogFileFilter>({{"Disassembly listings", "lst;txt"}, {"All files", "*"}});
    SDL_ShowOpenFileDialog(OnDialog, request.release(), &window, filters.data(), static_cast<int>(filters.size()), nullptr, false);
    return true;
}

bool sdl_file_dialog::SaveMemoryDump(const std::shared_ptr<State>& state, SDL_Window& window, preview_debugger::MemorySource source,
    std::uint16_t start, std::uint16_t end)
{
    if (!state || end <= start || source < preview_debugger::MemorySource::cpu || source > preview_debugger::MemorySource::rom) return false;
    auto request = std::make_unique<Request>(state, media_slots::Kind::floppy, static_cast<int>(source), Purpose::memoryDump, start, end);
    if (!state->Begin()) return false;
    static constexpr auto filters = std::to_array<SDL_DialogFileFilter>({{"Binary memory dump", "bin"}});
    SDL_ShowSaveFileDialog(OnDialog, request.release(), &window, filters.data(), static_cast<int>(filters.size()), nullptr);
    return true;
}

bool sdl_file_dialog::SaveDisplayList(const std::shared_ptr<State>& state, SDL_Window& window)
{
    if (!state) return false;
    auto request = std::make_unique<Request>(state, media_slots::Kind::floppy, -1, Purpose::displayListDump);
    if (!state->Begin()) return false;
    static constexpr auto filters = std::to_array<SDL_DialogFileFilter>({{"Text file", "txt"}});
    SDL_ShowSaveFileDialog(OnDialog, request.release(), &window, filters.data(), static_cast<int>(filters.size()), nullptr);
    return true;
}

bool sdl_file_dialog::SaveConsoleLog(const std::shared_ptr<State>& state, SDL_Window& window)
{
    if (!state) return false;
    auto request = std::make_unique<Request>(state, media_slots::Kind::floppy, -1, Purpose::consoleLog);
    if (!state->Begin()) return false;
    static constexpr auto filters = std::to_array<SDL_DialogFileFilter>({{"Text file", "txt"}});
    SDL_ShowSaveFileDialog(OnDialog, request.release(), &window, filters.data(), static_cast<int>(filters.size()), nullptr);
    return true;
}

bool sdl_file_dialog::SaveTrace(const std::shared_ptr<State>& state, SDL_Window& window, std::uint16_t mask)
{
    if (!state || !mask) return false;
    auto request = std::make_unique<Request>(state, media_slots::Kind::floppy, mask, Purpose::trace);
    if (!state->Begin()) return false;
    static constexpr auto filters = std::to_array<SDL_DialogFileFilter>({{"Trace log", "log;txt"}, {"All files", "*"}});
    SDL_ShowSaveFileDialog(OnDialog, request.release(), &window, filters.data(), static_cast<int>(filters.size()), nullptr);
    return true;
}
