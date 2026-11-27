/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "FrameWorker.h"
#include "BackgroundJob.h"
#include <stdexcept>
#include <utility>

FrameWorker::FrameWorker(std::function<bool()> task, std::function<void()> notify)
    : task_(std::move(task)), notify_(std::move(notify)),
      worker_([this](std::stop_token stop) { Run(stop); }) {}
FrameWorker::~FrameWorker() = default;
bool FrameWorker::Start()
{
    return Request({});
}
bool FrameWorker::Start(std::function<bool()> operation)
{
    return operation && Request(std::move(operation));
}
bool FrameWorker::Request(std::function<bool()> operation)
{
    const std::scoped_lock lock(mutex_);
    if (outstanding_) return false;
    operation_ = std::move(operation);
    outstanding_ = requested_ = true;
    completed_ = false;
    error_ = nullptr;
    changed_.notify_one();
    return true;
}
bool FrameWorker::Wait()
{
    std::unique_lock lock(mutex_);
    if (!outstanding_) throw std::logic_error("No task is outstanding");
    changed_.wait(lock, [this] { return completed_; });
    outstanding_ = false;
    if (error_) std::rethrow_exception(error_);
    return result_;
}
bool FrameWorker::WaitFor(std::chrono::milliseconds timeout)
{
    std::unique_lock lock(mutex_);
    if (!outstanding_) throw std::logic_error("No task is outstanding");
    return changed_.wait_for(lock, timeout, [this] { return completed_; });
}
FrameWorker::Outcome FrameWorker::WaitOutcome() noexcept
{
    try { return Wait() ? Outcome::completed : Outcome::stopped; }
    catch (...) { return Outcome::failed; }
}
void FrameWorker::Run(std::stop_token stop)
{
    for (;;)
    {
        std::function<bool()> operation;
        {
            std::unique_lock lock(mutex_);
            // The predicate still wins over a stop request, so queued work is drained.
            if (!changed_.wait(lock, stop, [this] { return requested_; })) return;
            requested_ = false;
            operation = std::exchange(operation_, {});
        }
        bool result = false;
        std::exception_ptr error{};
        try { result = operation ? operation() : task_(); } catch (...) { error = std::current_exception(); }
        operation = {};
        {
            const std::scoped_lock lock(mutex_);
            result_ = result;
            error_ = error;
            completed_ = true;
        }
        changed_.notify_all();
        background::Signal(notify_);
    }
}
