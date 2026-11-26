/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "SdlDisplay.h"
#include "BackgroundJob.h"
#include "FrameWorker.h"
#include "ScreenText.h"
#include <SDL3/SDL.h>
#include <gtest/gtest.h>
#include <array>
#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <stdexcept>
#include <thread>

namespace
{
TEST(SdlDisplay, FitsViewportBetweenBarsAtDifferentScales)
{
    const auto screen = sdl_display::Viewport(1280, 960, 2, 30, 20);
    EXPECT_NEAR(screen.x, 66.6667f, 0.001f);
    EXPECT_FLOAT_EQ(screen.y, 60);
    EXPECT_NEAR(screen.w, 1146.6666f, 0.001f);
    EXPECT_FLOAT_EQ(screen.h, 860);
    const auto tiny = sdl_display::Viewport(200, 20, 1, 40, 20);
    EXPECT_FLOAT_EQ(tiny.w, 0);
    EXPECT_FLOAT_EQ(tiny.h, 0);
    EXPECT_GE(tiny.y, 0);
}

class SdlDisplayTest : public testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "dummy", SDL_HINT_OVERRIDE));
        ASSERT_TRUE(SDL_Init(SDL_INIT_VIDEO)) << SDL_GetError();
    }
    void TearDown() override { SDL_Quit(); }
};

TEST_F(SdlDisplayTest, TogglesFullscreenAndRestoresWindow)
{
    const std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
        SDL_CreateWindow("test", 640, 480, SDL_WINDOW_HIDDEN), SDL_DestroyWindow);
    ASSERT_TRUE(window) << SDL_GetError();
    ASSERT_TRUE(sdl_display::ToggleFullscreen(*window)) << SDL_GetError();
    ASSERT_TRUE(SDL_SyncWindow(window.get())) << SDL_GetError();
    EXPECT_NE(SDL_GetWindowFlags(window.get()) & SDL_WINDOW_FULLSCREEN, 0u);
    ASSERT_TRUE(sdl_display::ToggleFullscreen(*window)) << SDL_GetError();
    ASSERT_TRUE(SDL_SyncWindow(window.get())) << SDL_GetError();
    EXPECT_EQ(SDL_GetWindowFlags(window.get()) & SDL_WINDOW_FULLSCREEN, 0u);
}

TEST_F(SdlDisplayTest, ClipboardAcceptsConvertedCyrillicAndSymbols)
{
    const std::array<std::uint8_t, 4> bytes{0xe1, 0xc1, 0x80, 0};
    const auto text = screen_text::ToUtf8(bytes);
    ASSERT_TRUE(SDL_SetClipboardText(text.c_str())) << SDL_GetError();
    const std::unique_ptr<char, decltype(&SDL_free)> copy(SDL_GetClipboardText(), SDL_free);
    ASSERT_TRUE(copy);
    EXPECT_EQ(std::string(copy.get()), "\xd0\x90\xd0\xb0\xe2\x86\x91");
}

TEST_F(SdlDisplayTest, WakesForEventsAndLeavesThemQueuedInOrder)
{
    SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
    const Uint32 type = SDL_RegisterEvents(1);
    ASSERT_NE(type, 0u);
    std::atomic<bool> pushed{false};
    std::jthread producer([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        SDL_Event event{};
        event.type = type;
        event.user.code = 7;
        if (!SDL_PushEvent(&event)) return;
        event.user.code = 9;
        pushed.store(SDL_PushEvent(&event));
    });
    EXPECT_TRUE(sdl_display::WaitForEvents(std::chrono::seconds(1)));
    producer.join();
    ASSERT_TRUE(pushed.load());
    // Both events are queued, but SDL_PollEvent can return false at the end
    // of an earlier pump cycle before it reaches a later producer event.
    std::array<SDL_Event, 2> events{};
    ASSERT_EQ(SDL_PeepEvents(events.data(), static_cast<int>(events.size()), SDL_GETEVENT, type, type), 2);
    EXPECT_EQ(events[0].user.code, 7);
    EXPECT_EQ(events[1].user.code, 9);
    EXPECT_FALSE(SDL_HasEvent(type));
    EXPECT_FALSE(sdl_display::WaitForEvents(std::chrono::nanoseconds::zero()));
}

TEST_F(SdlDisplayTest, BackgroundResultAndExceptionAreReadyWhenTheirWakeArrives)
{
    SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
    sdl_display::CompletionWake wake;
    background::Job<std::unique_ptr<int>> job(wake.Notifier());
    auto source = std::make_shared<int>(42);
    const std::weak_ptr<int> inputLifetime = source;
    job.Start([input = std::move(source)] { return std::make_unique<int>(*input); });
    ASSERT_TRUE(sdl_display::WaitForEvents(std::chrono::seconds(1)));
    SDL_Event event{};
    ASSERT_TRUE(SDL_PollEvent(&event));
    ASSERT_TRUE(wake.Consume(event.type));
    ASSERT_TRUE(job.Ready());
    EXPECT_TRUE(inputLifetime.expired());
    EXPECT_EQ(*job.Take(), 42);
    EXPECT_FALSE(job.Pending());

    job.Start([]() -> std::unique_ptr<int> { throw std::runtime_error("read failed"); });
    ASSERT_TRUE(SDL_WaitEventTimeout(&event, 1000));
    ASSERT_TRUE(wake.Consume(event.type));
    ASSERT_TRUE(job.Ready());
    EXPECT_THROW((void)job.Take(), std::runtime_error);
    EXPECT_FALSE(job.Pending());
}

TEST_F(SdlDisplayTest, WaitsForBackgroundWorkAndLeavesStartupEventsQueued)
{
    SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
    const std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
        SDL_CreateWindow("startup", 640, 480, SDL_WINDOW_HIDDEN), SDL_DestroyWindow);
    ASSERT_TRUE(window);
    SDL_Event input{}, close{};
    input.type = SDL_EVENT_TEXT_INPUT;
    input.text.text = "startup input";
    close.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED;
    close.window.windowID = SDL_GetWindowID(window.get());
    ASSERT_TRUE(SDL_PushEvent(&input));
    ASSERT_TRUE(SDL_PushEvent(&close));

    // Exercise timed polling with an already queued event and no worker wake.
    sdl_display::CompletionWake wake;
    background::Job<bool> job;
    std::promise<void> release;
    job.Start([ready = release.get_future().share()] {
        return ready.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
    });
    bool observed = false;
    sdl_display::WaitForCompletion(wake, [&](auto timeout) { return job.WaitFor(timeout); }, [&] {
        if (!observed && sdl_display::CloseRequested(*window))
        {
            observed = true;
            release.set_value();
        }
    });
    EXPECT_TRUE(observed);
    EXPECT_TRUE(job.Take());
    std::array<SDL_Event, 2> events{};
    ASSERT_EQ(SDL_PeepEvents(events.data(), static_cast<int>(events.size()), SDL_GETEVENT,
        SDL_EVENT_WINDOW_CLOSE_REQUESTED, SDL_EVENT_TEXT_INPUT), 2);
    EXPECT_EQ(events[0].type, SDL_EVENT_TEXT_INPUT);
    EXPECT_EQ(events[1].type, SDL_EVENT_WINDOW_CLOSE_REQUESTED);
}

TEST_F(SdlDisplayTest, CoalescesConcurrentWakesAndPreservesUnrelatedEvents)
{
    SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
    sdl_display::CompletionWake wake;
    const auto notify = wake.Notifier();
    ASSERT_TRUE(notify);
    {
        std::array<std::jthread, 4> producers;
        for (auto& producer : producers)
            producer = std::jthread([&] { for (int i = 0; i < 8; ++i) notify(); });
    }
    SDL_Event event{};
    ASSERT_TRUE(SDL_WaitEventTimeout(&event, 1000));
    ASSERT_TRUE(wake.Consume(event.type));
    EXPECT_FALSE(SDL_HasEvent(event.type));

    notify();
    ASSERT_TRUE(SDL_HasEvent(event.type));
    SDL_Event other{};
    other.type = SDL_RegisterEvents(1);
    ASSERT_NE(other.type, 0u);
    ASSERT_TRUE(SDL_PushEvent(&other));
    wake.Clear();
    EXPECT_FALSE(SDL_HasEvent(event.type));
    EXPECT_TRUE(SDL_HasEvent(other.type));
    notify();
    EXPECT_TRUE(SDL_HasEvent(event.type));
}

TEST_F(SdlDisplayTest, PersistentWorkerWakesTheWindowForEachCollectedResult)
{
    SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
    sdl_display::CompletionWake wake;
    int calls = 0;
    FrameWorker worker([&] { return ++calls == 1; }, wake.Notifier());
    constexpr std::array results{FrameWorker::Outcome::completed, FrameWorker::Outcome::stopped};
    for (const auto expected : results)
    {
        ASSERT_TRUE(worker.Start());
        SDL_Event event{};
        ASSERT_TRUE(SDL_WaitEventTimeout(&event, 1000));
        ASSERT_TRUE(wake.Consume(event.type));
        ASSERT_TRUE(worker.WaitFor(std::chrono::milliseconds(0)));
        EXPECT_EQ(worker.WaitOutcome(), expected);
    }
}

TEST_F(SdlDisplayTest, SlowWorkerArmsWakeAndDisarmsAfterHandoff)
{
    SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
    sdl_display::CompletionWake wake(false);
    const auto notify = wake.Notifier();
    ASSERT_TRUE(notify);
    notify();
    EXPECT_FALSE(SDL_HasEvents(SDL_EVENT_USER, SDL_EVENT_LAST));

    std::promise<void> release;
    FrameWorker worker([ready = release.get_future().share()] {
        return ready.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
    }, notify);
    ASSERT_TRUE(worker.Start());
    bool pumped = false;
    sdl_display::WaitUntilWorkerReady(worker, wake, [&] {
        if (pumped) return;
        pumped = true;
        release.set_value();
        EXPECT_TRUE(sdl_display::WaitForEvents(std::chrono::seconds(1)));
    });
    EXPECT_TRUE(pumped);
    EXPECT_TRUE(worker.WaitFor(std::chrono::milliseconds(0)));
    EXPECT_EQ(worker.WaitOutcome(), FrameWorker::Outcome::completed);
    notify();
    EXPECT_FALSE(SDL_HasEvents(SDL_EVENT_USER, SDL_EVENT_LAST));
}

TEST_F(SdlDisplayTest, BoardWaitPreservesInputAndDropPayloadsInQueueOrder)
{
    SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
    sdl_display::CompletionWake wake(false);
    std::promise<void> release;
    FrameWorker worker([ready = release.get_future().share()] {
        return ready.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
    }, wake.Notifier());
    ASSERT_TRUE(worker.Start());
    bool pumped = false;
    const auto outcome = sdl_display::WaitForWorker(worker, wake, [&] {
        if (pumped) return;
        pumped = true;
        SDL_Event text{}, drop{};
        text.type = SDL_EVENT_TEXT_INPUT;
        text.text.text = "command";
        drop.type = SDL_EVENT_DROP_FILE;
        drop.drop.data = "disk.dsk";
        EXPECT_TRUE(SDL_PushEvent(&text));
        EXPECT_TRUE(SDL_PushEvent(&drop));
        release.set_value();
    });
    EXPECT_TRUE(pumped);
    EXPECT_EQ(outcome, FrameWorker::Outcome::completed);
    std::array<SDL_Event, 2> events{};
    ASSERT_EQ(SDL_PeepEvents(events.data(), static_cast<int>(events.size()), SDL_GETEVENT,
        SDL_EVENT_TEXT_INPUT, SDL_EVENT_DROP_FILE), 2);
    ASSERT_EQ(events[0].type, SDL_EVENT_TEXT_INPUT);
    EXPECT_STREQ(events[0].text.text, "command");
    ASSERT_EQ(events[1].type, SDL_EVENT_DROP_FILE);
    EXPECT_STREQ(events[1].drop.data, "disk.dsk");
    EXPECT_FALSE(SDL_HasEvents(SDL_EVENT_USER, SDL_EVENT_LAST));
}

TEST_F(SdlDisplayTest, BoardWaitCollectsWorkerFailureWithoutACompletionEvent)
{
    SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
    sdl_display::CompletionWake wake(false);
    std::promise<void> release;
    FrameWorker worker([ready = release.get_future().share()]() -> bool {
        if (ready.wait_for(std::chrono::seconds(2)) != std::future_status::ready) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
        throw std::runtime_error("guest I/O failed");
    });
    ASSERT_TRUE(worker.Start());
    bool pumped = false;
    const auto result = sdl_display::WaitForWorker(worker, wake, [&] {
        if (pumped) return;
        pumped = true;
        release.set_value();
    });
    EXPECT_TRUE(pumped);
    EXPECT_EQ(result, FrameWorker::Outcome::failed);
}

TEST_F(SdlDisplayTest, CompletionCallbackCanOutliveItsOwner)
{
    SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
    background::Notify notify;
    Uint32 type = 0;
    {
        sdl_display::CompletionWake wake;
        notify = wake.Notifier();
        ASSERT_TRUE(notify);
        notify();
        SDL_Event event{};
        ASSERT_TRUE(SDL_WaitEventTimeout(&event, 1000));
        ASSERT_TRUE(wake.Consume(event.type));
        type = event.type;
        notify();
        ASSERT_TRUE(SDL_HasEvent(type));
    }
    EXPECT_FALSE(SDL_HasEvent(type));
    std::jthread late(notify);
    late.join();
    EXPECT_FALSE(SDL_HasEvent(type));
}
}
