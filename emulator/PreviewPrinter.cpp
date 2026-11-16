/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "PreviewPrinter.h"
#include "emubase/Board.h"

preview_printer::Session::~Session() { (void)Stop(); }

void preview_printer::Session::Synchronize()
{
    board_.SetParallelOutCallbackWithContext(Active() ? Write : nullptr, Active() ? this : nullptr);
}

bool preview_printer::Session::Start(const std::filesystem::path& path)
{
    if (Active() || path.empty()) return false;
    auto savedPath = path;
    if (!output_.Open(path)) return false;
    path_ = std::move(savedPath);
    board_.SetParallelOutCallbackWithContext(Write, this);
    return true;
}

bool preview_printer::Session::Write(void* context, std::uint8_t value)
{
    return static_cast<Session*>(context)->output_.Write(value);
}

bool preview_printer::Session::Stop()
{
    if (!Active()) return true;
    board_.SetParallelOutCallbackWithContext(nullptr, nullptr);
    const bool closed = output_.Close();
    path_.clear();
    const bool failed = output_.TakeError();
    return closed && !failed;
}

bool preview_printer::Session::Flush()
{
    if (!Active()) return true;
    (void)output_.Flush();
    return Check();
}

bool preview_printer::Session::Check()
{
    if (!output_.TakeError()) return true;
    (void)Stop();
    return false;
}

bool preview_printer::Session::Conflicts(const std::filesystem::path& path) const
{
    if (!Active() || path.empty()) return false;
    if (path_ == path) return true;
    std::error_code error;
    return std::filesystem::equivalent(path_, path, error) && !error;
}
