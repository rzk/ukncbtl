/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "PreviewTape.h"
#include "FrameWorker.h"
#include "emubase/Board.h"
#include <chrono>
#include <utility>
#ifdef UKNCBTL_TAPE_TEST_HOOKS
#include "TapeTestHooks.h"
#include <atomic>

namespace
{
std::atomic<bool> failRecordingWrites{false};
}

void preview_tape::testing::FailRecordingWrites(bool fail)
{
    failRecordingWrites.store(fail);
}
#endif

class preview_tape::Session::RecordingWriter
{
public:
    explicit RecordingWriter(WavPcmFile file)
        : file_(std::move(file)), limit_(file_.RecordingLimit()), frequency_(file_.Frequency()),
          writer_([this] {
#ifdef UKNCBTL_TAPE_TEST_HOOKS
              if (failRecordingWrites.load()) return false;
#endif
              const auto& block = blocks_[writing_];
              return file_.WriteSamples(std::span{block.samples}.first(block.size)).has_value();
          }) {}

    [[nodiscard]] std::expected<void, WavError> WriteOne(std::uint32_t value)
    {
        if (failed_) return std::unexpected(WavError::io_error);
        if (position_ >= limit_) return std::unexpected(WavError::invalid_position);
        if (blocks_[filling_].size == BlockSize)
        {
            Collect(true);
            Submit();
        }
        if (failed_) return std::unexpected(WavError::io_error);
        auto& block = blocks_[filling_];
        block.samples[block.size++] = static_cast<std::uint8_t>(value >> 24);
        ++position_;
        length_ = (std::max)(length_, position_);
        return {};
    }

    [[nodiscard]] bool Check()
    {
        Collect(false);
        return !failed_;
    }

    [[nodiscard]] bool Sync()
    {
        Drain();
        if (!file_.Sync()) failed_ = true;
        return !failed_;
    }

    [[nodiscard]] bool Rewind()
    {
        Drain();
        if (failed_ || !file_.SetPosition(0)) return false;
        position_ = 0;
        return true;
    }

    [[nodiscard]] bool Close()
    {
        Drain();
        const bool closed = file_.Close().has_value();
        return closed && !failed_;
    }

    [[nodiscard]] bool Failed() const { return failed_; }
    [[nodiscard]] std::uint32_t Position() const { return position_; }
    [[nodiscard]] std::uint32_t Length() const { return length_; }
    [[nodiscard]] int Frequency() const { return frequency_; }

private:
    static constexpr std::size_t BlockSize = 4096;
    struct Block
    {
        std::array<std::uint8_t, BlockSize> samples{};
        std::size_t size = 0;
    };
    void Submit()
    {
        if (pending_ || failed_ || blocks_[filling_].size == 0) return;
        // The writer owns the submitted block and file until collection.
        std::swap(filling_, writing_);
        pending_ = writer_.Start();
        if (!pending_) MarkFailed();
    }
    void Collect(bool wait)
    {
        if (!pending_ || (!wait && !writer_.WaitFor(std::chrono::milliseconds(0)))) return;
        const auto outcome = writer_.WaitOutcome();
        pending_ = false;
        blocks_[writing_].size = 0;
        if (outcome != FrameWorker::Outcome::completed) MarkFailed();
    }
    void MarkFailed()
    {
        failed_ = true;
        blocks_[filling_].size = 0;
        position_ = file_.Position();
        length_ = file_.Length();
    }
    void Drain()
    {
        Collect(true);
        Submit();
        Collect(true);
    }

    WavPcmFile file_;
    const std::uint32_t limit_;
    const int frequency_;
    std::array<Block, 2> blocks_;
    std::size_t filling_ = 0, writing_ = 1;
    std::uint32_t position_ = 0, length_ = 0;
    bool pending_ = false, failed_ = false;
    FrameWorker writer_;
};

preview_tape::Session::Session(CMotherboard& board) : board_(board) {}
preview_tape::Session::~Session() { (void)Close(); }

bool preview_tape::Session::Open(const std::filesystem::path& path)
{
    if (Inserted() || path.empty()) return false;
    auto saved = path;
    auto file = WavPcmFile::Open(path);
    if (!file) return false;
    return Insert(std::move(saved), std::move(*file), FileMode::playback);
}

bool preview_tape::Session::Insert(std::filesystem::path path, WavPcmFile file, FileMode mode)
{
    if (mode == FileMode::recording)
    {
        try { recording_ = std::make_unique<RecordingWriter>(std::move(file)); }
        catch (...) { return false; }
    }
    else file_.emplace(std::move(file));
    path_ = std::move(path);
    waveform_.fill(128);
    state_.Insert(mode == FileMode::recording);
    return true;
}

preview_tape::AsyncPrepare::Result::Result(
    std::filesystem::path source, std::expected<WavPcmFile, WavError> file, FileMode fileMode)
    : path(std::move(source)), mode(fileMode)
{
    if (file) stream.emplace(std::move(*file));
}

bool preview_tape::AsyncPrepare::Result::InsertInto(Session& session) &&
{
    if (!stream || session.Inserted()) return false;
    const bool inserted = session.Insert(std::move(path), std::move(*stream), mode);
    stream.reset();
    return inserted;
}

bool preview_tape::AsyncPrepare::Result::Close()
{
    if (!stream) return false;
    const bool closed = static_cast<bool>(stream->Close());
    stream.reset();
    return closed;
}

bool preview_tape::AsyncPrepare::Start(std::filesystem::path path, FileMode mode)
{
    if (Pending() || path.empty()) return false;
    try
    {
        path_ = std::move(path);
        result_ = std::async(std::launch::async, [source = path_, mode]() -> std::expected<WavPcmFile, WavError> {
            try
            {
                if (mode == FileMode::playback) return WavPcmFile::Open(source);
                auto file = WavPcmFile::Create(source, 44100);
                if (file && !file->Sync()) return std::unexpected(WavError::io_error);
                return file;
            }
            catch (...) { return std::unexpected(WavError::io_error); }
        });
        mode_ = mode;
        return true;
    }
    catch (...) { path_.clear(); return false; }
}

std::optional<preview_tape::AsyncPrepare::Result> preview_tape::AsyncPrepare::Poll()
{
    if (!Pending() || result_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return std::nullopt;
    return Finish();
}

std::optional<preview_tape::AsyncPrepare::Result> preview_tape::AsyncPrepare::Finish()
{
    if (!Pending()) return std::nullopt;
    auto result = result_.get();
    auto source = std::move(path_);
    path_.clear();
    return Result(std::move(source), std::move(result), mode_);
}

bool preview_tape::Session::Create(const std::filesystem::path& path)
{
    if (Inserted() || path.empty()) return false;
    auto saved = path;
    auto file = WavPcmFile::Create(path, 44100);
    if (!file) return false;
    return Insert(std::move(saved), std::move(*file), FileMode::recording);
}

std::uint32_t preview_tape::Session::Position() const
{
    return recording_ ? recording_->Position() : file_ ? file_->Position() : 0;
}

std::uint32_t preview_tape::Session::Length() const
{
    return recording_ ? recording_->Length() : file_ ? file_->Length() : 0;
}

int preview_tape::Session::Frequency() const
{
    return recording_ ? recording_->Frequency() : file_ ? file_->Frequency() : 0;
}

bool preview_tape::Session::Close()
{
    Stop();
    const bool closed = recording_ ? recording_->Close() : !file_ || static_cast<bool>(file_->Close());
    recording_.reset(); file_.reset(); path_.clear(); state_.Eject(); waveform_.fill(128);
    const bool failed = std::exchange(error_, false);
    return closed && !failed;
}

bool preview_tape::Session::Play()
{
    if ((recording_ && recording_->Failed()) || !state_.Play()) return false;
    if (Recording()) board_.SetTapeWriteCallbackWithContext(Write, this, recording_->Frequency());
    else board_.SetTapeReadCallbackWithContext(Read, this, file_->Frequency());
    return true;
}

void preview_tape::Session::Stop()
{
    if (!state_.Stop()) return;
    if (Recording())
    {
        board_.SetTapeWriteCallbackWithContext(nullptr, nullptr, 0);
        if (!recording_->Sync()) error_ = true;
    }
    else board_.SetTapeReadCallbackWithContext(nullptr, nullptr, 0);
}

bool preview_tape::Session::Rewind()
{
    const bool rewound = recording_ ? recording_->Rewind() : file_ && file_->SetPosition(0);
    if (!rewound) { Stop(); return false; }
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
    const auto result = tape::Write(*self.recording_, static_cast<std::uint32_t>(value), samples, self.waveform_);
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

bool preview_tape::Session::TakeError()
{
    if (recording_ && Playing() && !recording_->Check()) Stop();
    return std::exchange(error_, false);
}
