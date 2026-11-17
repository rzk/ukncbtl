/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "PreviewTape.h"
#include "emubase/Board.h"
#include <utility>

preview_tape::Session::~Session() { (void)Close(); }

bool preview_tape::Session::Open(const std::filesystem::path& path)
{
    if (Inserted() || path.empty()) return false;
    auto saved = path;
    auto file = WavPcmFile::Open(path);
    if (!file) return false;
    file_.emplace(std::move(*file));
    path_ = std::move(saved);
    waveform_.fill(128);
    state_.Insert(false);
    return true;
}

bool preview_tape::Session::Create(const std::filesystem::path& path)
{
    if (Inserted() || path.empty()) return false;
    auto saved = path;
    auto file = WavPcmFile::Create(path, 44100);
    if (!file) return false;
    file_.emplace(std::move(*file));
    path_ = std::move(saved);
    waveform_.fill(128);
    state_.Insert(true);
    return true;
}

bool preview_tape::Session::Close()
{
    Stop();
    const bool closed = !file_ || static_cast<bool>(file_->Close());
    file_.reset(); path_.clear(); state_.Eject(); waveform_.fill(128);
    const bool failed = std::exchange(error_, false);
    return closed && !failed;
}

bool preview_tape::Session::Play()
{
    if (!state_.Play()) return false;
    if (Recording()) board_.SetTapeWriteCallbackWithContext(Write, this, file_->Frequency());
    else board_.SetTapeReadCallbackWithContext(Read, this, file_->Frequency());
    return true;
}

void preview_tape::Session::Stop()
{
    if (!state_.Stop()) return;
    if (Recording())
    {
        board_.SetTapeWriteCallbackWithContext(nullptr, nullptr, 0);
        if (file_ && !file_->Sync()) error_ = true;
    }
    else board_.SetTapeReadCallbackWithContext(nullptr, nullptr, 0);
}

bool preview_tape::Session::Rewind()
{
    if (!file_ || !file_->SetPosition(0)) { Stop(); return false; }
    waveform_.fill(128);
    return true;
}

bool preview_tape::Session::Seek(std::uint32_t position)
{
    if (!file_ || Recording() || Playing() || !file_->SetPosition(position)) return false;
    waveform_.fill(128);
    return true;
}

bool preview_tape::Session::Read(void* context, unsigned samples)
{
    auto& self = *static_cast<Session*>(context);
    const auto result = tape::Read(*self.file_, samples, self.waveform_);
    if (result.stop) self.Stop();
    self.error_ |= result.error.has_value();
    return result.bit;
}

void preview_tape::Session::Write(void* context, int value, unsigned samples)
{
    auto& self = *static_cast<Session*>(context);
    const auto result = tape::Write(*self.file_, static_cast<std::uint32_t>(value), samples, self.waveform_);
    if (result.stop) self.Stop();
    self.error_ |= result.error.has_value();
}

bool preview_tape::Session::Conflicts(const std::filesystem::path& path) const
{
    if (!Inserted() || path.empty()) return false;
    if (path_ == path) return true;
    std::error_code error;
    return std::filesystem::equivalent(path_, path, error) && !error;
}

bool preview_tape::Session::TakeError() { return std::exchange(error_, false); }
