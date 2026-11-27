/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "PreviewPrinter.h"
#include "util/PathIdentity.h"
#include "FrameWorker.h"
#include "emubase/Board.h"
#include <algorithm>
#include <utility>
#ifdef UKNCBTL_PRINTER_TEST_HOOKS
#include "PrinterTestHooks.h"
#include <atomic>

namespace
{
std::atomic<bool> failWorkerWrites{false};
}

void preview_printer::testing::FailWorkerWrites(bool fail)
{
    failWorkerWrites.store(fail);
}
#endif

preview_printer::Session::Session(CMotherboard& board) : board_(board) {}
preview_printer::Session::~Session() { (void)Stop(); }

void preview_printer::Session::Synchronize()
{
    board_.SetParallelOutCallbackWithContext(Active() ? Write : nullptr, Active() ? this : nullptr);
}

bool preview_printer::Session::Start(const std::filesystem::path& path)
{
    if (Active() || path.empty()) return false;
    auto savedPath = path;
    std::unique_ptr<FrameWorker> writer;
    try
    {
        writer = std::make_unique<FrameWorker>([this] {
#ifdef UKNCBTL_PRINTER_TEST_HOOKS
            if (failWorkerWrites.load()) return false;
#endif
            return output_.Write(std::span{writing_}.first(writingSize_));
        });
    }
    catch (...) { return false; }
    if (!output_.Open(path)) return false;
    writer_ = std::move(writer);
    path_ = std::move(savedPath);
    board_.SetParallelOutCallbackWithContext(Write, this);
    return true;
}

bool preview_printer::Session::Write(void* context, std::uint8_t value)
{
    return static_cast<Session*>(context)->QueueByte(value);
}

bool preview_printer::Session::QueueByte(std::uint8_t value)
{
    if (queuedSize_ == BatchSize)
    {
        // Backpressure bounds memory if the host cannot keep up.
        Collect(true);
        Submit();
    }
    if (failed_) return false;
    queued_[queuedSize_++] = value;
    return true;
}

void preview_printer::Session::Submit()
{
    if (pending_ || failed_ || queuedSize_ == 0) return;
    std::copy_n(queued_.begin(), queuedSize_, writing_.begin());
    writingSize_ = std::exchange(queuedSize_, 0);
    pending_ = writer_->Start();
    failed_ = !pending_;
}

void preview_printer::Session::Collect(bool wait)
{
    if (!pending_ || (!wait && !writer_->WaitFor(std::chrono::milliseconds(0)))) return;
    failed_ = writer_->WaitOutcome() != FrameWorker::Outcome::completed;
    pending_ = false;
}

void preview_printer::Session::Drain()
{
    Collect(true);
    Submit();
    Collect(true);
}

bool preview_printer::Session::Stop()
{
    if (!Active()) return true;
    board_.SetParallelOutCallbackWithContext(nullptr, nullptr);
    Drain();
    writer_.reset();
    const bool closed = output_.Close();
    path_.clear();
    const bool outputFailed = output_.TakeError();
    const bool workerFailed = std::exchange(failed_, false);
    queuedSize_ = 0;
    return closed && !outputFailed && !workerFailed;
}

bool preview_printer::Session::Flush()
{
    if (!Active()) return true;
    Drain();
    if (!failed_ && output_.Flush()) return true;
    (void)Stop();
    return false;
}

bool preview_printer::Session::Check()
{
    if (!Active()) return true;
    Collect(false);
    Submit();
    if (failed_)
    {
        (void)Stop();
        return false;
    }
    return true;
}

bool preview_printer::Session::Conflicts(const std::filesystem::path& path) const
{
    if (!Active() || path.empty()) return false;
    return path_identity::SameFile(path_, path);
}
