/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once
#include <chrono>
#include <functional>
#include <future>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace background
{
using Notify = std::function<void()>;

inline void Signal(const Notify& notify) noexcept
{
    if (!notify) return;
    try { notify(); }
    catch (...) {} // A failed wake leaves the result available for polling.
}

// The owner starts and collects one job at a time. The worker publishes the
// result before notifying, and destruction waits for work and notification.
template<class T>
class Job
{
public:
    explicit Job(Notify notify = {}) : notify_(std::move(notify)) {}

    template<class Work>
    void Start(Work&& work)
    {
        if (Pending()) throw std::logic_error("A background result is pending");
        if (!notify_)
        {
            result_ = std::async(std::launch::async, std::forward<Work>(work));
            return;
        }
        using Function = std::decay_t<Work>;
        std::packaged_task<T()> task([work = std::optional<Function>(std::in_place, std::forward<Work>(work))]() mutable -> T {
            // Release the captured inputs before the result is published.
            auto run = std::move(*work);
            work.reset();
            return std::invoke(std::move(run));
        });
        auto result = task.get_future();
        auto runner = std::async(std::launch::async, [task = std::move(task), notify = notify_]() mutable {
            task();
            Signal(notify);
        });
        runner_ = std::move(runner);
        result_ = std::move(result);
    }

    [[nodiscard]] bool Pending() const { return result_.valid(); }
    [[nodiscard]] bool WaitFor(std::chrono::milliseconds timeout) const
    { return Pending() && result_.wait_for(timeout) == std::future_status::ready; }
    [[nodiscard]] bool Ready() const { return WaitFor(std::chrono::milliseconds(0)); }
    [[nodiscard]] T Take() { return result_.get(); }

private:
    Notify notify_;
    std::future<T> result_;
    std::future<void> runner_;
};
}
