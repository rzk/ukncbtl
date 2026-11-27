/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "SdlDisplay.h"
#include <SDL3/SDL_events.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <mutex>

bool sdl_display::ToggleFullscreen(SDL_Window& window)
{
    return SDL_SetWindowFullscreen(&window, (SDL_GetWindowFlags(&window) & SDL_WINDOW_FULLSCREEN) == 0);
}

bool sdl_display::WaitForEvents(std::chrono::nanoseconds timeout)
{
    if (timeout <= std::chrono::nanoseconds::zero()) return false;
    using Milliseconds = std::chrono::milliseconds;
    const auto milliseconds = std::chrono::ceil<Milliseconds>(timeout).count();
    const auto limit = (std::numeric_limits<Sint32>::max)();
    return SDL_WaitEventTimeout(nullptr, static_cast<Sint32>(std::min<Milliseconds::rep>(milliseconds, limit)));
}

struct sdl_display::CompletionWake::State
{
    explicit State(bool active) : enabled(active && type != 0) {}
    const Uint32 type = SDL_RegisterEvents(1);
    std::mutex mutex;
    bool enabled;
    bool queued = false;
};

sdl_display::CompletionWake::CompletionWake(bool enabled) : state_(std::make_shared<State>(enabled)) {}

sdl_display::CompletionWake::~CompletionWake() { SetEnabled(false); }

void sdl_display::CompletionWake::SetEnabled(bool enabled)
{
    const std::scoped_lock lock(state_->mutex);
    state_->enabled = enabled && state_->type != 0;
    if (!state_->enabled && state_->queued)
    {
        SDL_FlushEvent(state_->type);
        state_->queued = false;
    }
}

std::function<void()> sdl_display::CompletionWake::Notifier() const
{
    if (!state_->type) return {};
    return [state = state_] {
        const std::scoped_lock lock(state->mutex);
        if (!state->enabled || state->queued) return;
        SDL_Event event{};
        event.type = state->type;
        state->queued = SDL_PushEvent(&event);
    };
}

void sdl_display::CompletionWake::Clear()
{
    const std::scoped_lock lock(state_->mutex);
    if (state_->queued) SDL_FlushEvent(state_->type);
    state_->queued = false;
}

bool sdl_display::CompletionWake::Consume(Uint32 type)
{
    if (!type || type != state_->type) return false;
    const std::scoped_lock lock(state_->mutex);
    state_->queued = false;
    return true;
}

bool sdl_display::CloseRequested(SDL_Window& window)
{
    if (SDL_HasEvent(SDL_EVENT_QUIT)) return true;
    SDL_Event event{};
    return SDL_PeepEvents(&event, 1, SDL_PEEKEVENT, SDL_EVENT_WINDOW_CLOSE_REQUESTED,
        SDL_EVENT_WINDOW_CLOSE_REQUESTED) == 1 && event.window.windowID == SDL_GetWindowID(&window);
}

void sdl_display::WaitForCompletion(CompletionWake& wake,
    const std::function<bool(std::chrono::milliseconds)>& ready, const std::function<void()>& afterPump)
{
    using namespace std::chrono_literals;
    for (;;)
    {
        SDL_PumpEvents();
        wake.Clear();
        if (afterPump) afterPump();
        if (ready(0ms)) return;
        if (WaitForEvents(5ms) && ready(5ms)) return;
    }
}

void sdl_display::WaitUntilWorkerReady(FrameWorker& worker, CompletionWake& wake,
    const std::function<void()>& afterPump)
{
    using namespace std::chrono_literals;
    if (worker.WaitFor(1ms)) return;

    wake.SetEnabled(true);
    struct Disarm
    {
        CompletionWake& wake;
        ~Disarm() { wake.SetEnabled(false); }
    } disarm{wake};
    WaitForCompletion(wake, [&](auto timeout) { return worker.WaitFor(timeout); }, afterPump);
}

FrameWorker::Outcome sdl_display::WaitForWorker(FrameWorker& worker, CompletionWake& wake,
    const std::function<void()>& afterPump)
{
    WaitUntilWorkerReady(worker, wake, afterPump);
    return worker.WaitOutcome();
}

SDL_FRect sdl_display::Viewport(float width, float height, float scaleY,
    float topUi, float bottomUi, int screenWidth, int screenHeight)
{
    width = std::max(0.0f, width);
    height = std::max(0.0f, height);
    scaleY = std::max(0.0f, scaleY);
    const float top = std::min(height, std::ceil(std::max(0.0f, topUi) * scaleY));
    const float bottom = std::floor(height - std::max(0.0f, bottomUi) * scaleY);
    const float availableHeight = std::max(0.0f, bottom - top);
    const float drawWidth = std::min(width, static_cast<float>(screenWidth));
    const float drawHeight = std::min(availableHeight, static_cast<float>(screenHeight));
    return {std::floor((width - drawWidth) / 2.0f), top + std::floor((availableHeight - drawHeight) / 2.0f),
        drawWidth, drawHeight};
}
