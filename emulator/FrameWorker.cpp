/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "FrameWorker.h"
#include <stdexcept>
#include <utility>

FrameWorker::FrameWorker(std::function<bool()> task) : task_(std::move(task)), worker_(&FrameWorker::Run, this) {}
FrameWorker::~FrameWorker()
{
    { const std::lock_guard lock(mutex_); stopping_ = true; }
    changed_.notify_one();
    worker_.join();
}
bool FrameWorker::Start()
{
    const std::lock_guard lock(mutex_);
    if (outstanding_ || stopping_) return false;
    outstanding_ = requested_ = true;
    completed_ = false;
    error_ = nullptr;
    changed_.notify_one();
    return true;
}
bool FrameWorker::Wait()
{
    std::unique_lock lock(mutex_);
    if (!outstanding_) throw std::logic_error("No frame is outstanding");
    changed_.wait(lock, [this] { return completed_; });
    outstanding_ = false;
    if (error_) std::rethrow_exception(error_);
    return result_;
}
bool FrameWorker::WaitFor(std::chrono::milliseconds timeout)
{
    std::unique_lock lock(mutex_);
    if (!outstanding_) throw std::logic_error("No frame is outstanding");
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
        lock.unlock();
        bool result = false;
        std::exception_ptr error;
        try { result = task_(); } catch (...) { error = std::current_exception(); }
        lock.lock();
        result_ = result;
        error_ = error;
        completed_ = true;
        changed_.notify_all();
    }
}
