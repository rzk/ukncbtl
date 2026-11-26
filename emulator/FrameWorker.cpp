/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "FrameWorker.h"
#include "BackgroundJob.h"
#include <stdexcept>
#include <utility>

FrameWorker::FrameWorker(std::function<bool()> task, std::function<void()> notify)
    : task_(std::move(task)), notify_(std::move(notify)), worker_(&FrameWorker::Run, this) {}
FrameWorker::~FrameWorker()
{
    { const std::lock_guard lock(mutex_); stopping_ = true; }
    changed_.notify_one();
    worker_.join();
}
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
    const std::lock_guard lock(mutex_);
    if (outstanding_ || stopping_) return false;
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
void FrameWorker::Run()
{
    std::unique_lock lock(mutex_);
    for (;;)
    {
        changed_.wait(lock, [this] { return requested_ || stopping_; });
        if (!requested_ && stopping_) return;
        requested_ = false;
        auto operation = std::exchange(operation_, {});
        lock.unlock();
        bool result = false;
        std::exception_ptr error;
        try { result = operation ? operation() : task_(); } catch (...) { error = std::current_exception(); }
        operation = {};
        lock.lock();
        result_ = result;
        error_ = error;
        completed_ = true;
        changed_.notify_all();
        if (notify_)
        {
            lock.unlock();
            background::Signal(notify_);
            lock.lock();
        }
    }
}
