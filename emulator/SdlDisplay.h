/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once
#include "FrameWorker.h"
#include <SDL3/SDL_video.h>
#include <SDL3/SDL_rect.h>
#include <chrono>
#include <functional>
#include <memory>

namespace sdl_display
{
// SDL may apply the requested fullscreen change asynchronously.
[[nodiscard]] bool ToggleFullscreen(SDL_Window& window);
// Wait on the SDL main thread. A ready event stays queued for the caller.
[[nodiscard]] bool WaitForEvents(std::chrono::nanoseconds timeout);
// One queued SDL event represents any number of completed background jobs.
// Callbacks may outlive the window. Destroying the owner disables them.
class CompletionWake
{
public:
    explicit CompletionWake(bool enabled = true);
    ~CompletionWake();
    CompletionWake(const CompletionWake&) = delete;
    CompletionWake& operator=(const CompletionWake&) = delete;
    [[nodiscard]] std::function<void()> Notifier() const;
    // Disabling also removes any queued wake.
    void SetEnabled(bool enabled);
    // Clear the previous wake before polling results on the main thread.
    void Clear();
    // Rearm after a wake event is removed during the regular event loop.
    [[nodiscard]] bool Consume(Uint32 type);
private:
    struct State;
    std::shared_ptr<State> state_;
};
// The wake belongs to the caller. A timed check bounds the wait when the SDL
// queue already holds input or cannot accept the completion event.
void WaitForCompletion(CompletionWake& wake, const std::function<bool(std::chrono::milliseconds)>& ready,
    const std::function<void()>& afterPump = {});
[[nodiscard]] bool CloseRequested(SDL_Window& window);
// Wait on the SDL thread. Pass a dedicated wake initialized disabled and use
// its notifier for this worker. Ordinary events stay queued until handoff.
// WaitUntilWorkerReady leaves the result for the caller to collect.
void WaitUntilWorkerReady(FrameWorker& worker, CompletionWake& wake, const std::function<void()>& afterPump = {});
[[nodiscard]] FrameWorker::Outcome WaitForWorker(FrameWorker& worker, CompletionWake& wake,
    const std::function<void()>& afterPump = {});
// UI heights are logical coordinates. Center the screen at its selected pixel
// size where it fits, and return the visible portion starting at source (0, 0).
[[nodiscard]] SDL_FRect Viewport(float width, float height, float scaleY,
    float topUi, float bottomUi, int screenWidth, int screenHeight);
}
