/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once
#include <condition_variable>
#include <chrono>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>

// One task at a time, with externally serialized calls to the control methods.
// The caller must not access objects used by the task between Start and Wait.
// The task and its dependencies must outlive the worker.
class FrameWorker
{
public:
    enum class Outcome { completed, stopped, failed };
    explicit FrameWorker(std::function<bool()> task);
    ~FrameWorker();
    FrameWorker(const FrameWorker&) = delete;
    FrameWorker& operator=(const FrameWorker&) = delete;
    [[nodiscard]] bool Start();
    // Wait without consuming the result, so a host UI can pump events while
    // the board is owned by the worker. Requires an outstanding frame.
    [[nodiscard]] bool WaitFor(std::chrono::milliseconds timeout);
    [[nodiscard]] bool Wait();
    // Converts task exceptions to a failure result for frontend error handling.
    [[nodiscard]] Outcome WaitOutcome() noexcept;
private:
    void Run();
    std::function<bool()> task_;
    std::mutex mutex_;
    std::condition_variable changed_;
    bool stopping_ = false;
    bool outstanding_ = false;
    bool requested_ = false;
    bool completed_ = false;
    bool result_ = false;
    std::exception_ptr error_;
    std::thread worker_;
};

// A failed frame may leave partially advanced state. A breakpoint is a normal
// stop, and must not clear a previous failure. Only an explicit recovery does.
class FrameFailureLatch
{
public:
    void Observe(FrameWorker::Outcome outcome) noexcept
    { if (outcome == FrameWorker::Outcome::failed) failed_ = true; }
    void Recovered() noexcept { failed_ = false; }
    [[nodiscard]] bool Failed() const noexcept { return failed_; }
private:
    bool failed_ = false;
};
