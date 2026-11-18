/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once
#include "TapeTransport.h"
#include <filesystem>
#include <future>
#include <memory>
#include <optional>
#include <array>
#include <span>

class CMotherboard;
namespace preview_tape
{
class Session;
enum class FileMode { playback, recording };

// Transfers an opened playback or recording stream to the board owner at a frame
// boundary. Only the worker accesses the stream until the result is collected.
class AsyncPrepare
{
public:
    struct Result
    {
        [[nodiscard]] bool Ready() const { return stream.has_value(); }
        [[nodiscard]] FileMode Mode() const { return mode; }
        [[nodiscard]] bool InsertInto(Session& session) &&;
        [[nodiscard]] bool Close();
    private:
        friend class AsyncPrepare;
        Result(std::filesystem::path path, std::expected<WavPcmFile, WavError> file, FileMode mode);
        std::filesystem::path path;
        std::optional<WavPcmFile> stream;
        FileMode mode = FileMode::playback;
    };
    [[nodiscard]] bool Start(std::filesystem::path path, FileMode mode);
    [[nodiscard]] bool Pending() const { return result_.valid(); }
    [[nodiscard]] const std::filesystem::path& Path() const { return path_; }
    [[nodiscard]] FileMode Mode() const { return mode_; }
    [[nodiscard]] std::optional<Result> Poll();
    [[nodiscard]] std::optional<Result> Finish();
private:
    std::filesystem::path path_;
    FileMode mode_ = FileMode::playback;
    std::future<std::expected<WavPcmFile, WavError>> result_;
};

// Owns the board tape callback. The callback runs during a worker-owned frame;
// callers must serialize session operations with that frame's completion.
class Session
{
public:
    explicit Session(CMotherboard& board);
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    [[nodiscard]] bool Open(const std::filesystem::path& path);
    [[nodiscard]] bool Create(const std::filesystem::path& path);
    [[nodiscard]] bool Close();
    [[nodiscard]] bool Play();
    void Stop();
    [[nodiscard]] bool Rewind();
    // Seeking is only supported for stopped playback, never for recordings.
    [[nodiscard]] bool Seek(std::uint32_t position);
    [[nodiscard]] bool Inserted() const { return state_.Inserted(); }
    [[nodiscard]] bool Playing() const { return state_.Playing(); }
    [[nodiscard]] bool Recording() const { return state_.Recording(); }
    [[nodiscard]] std::uint32_t Position() const;
    [[nodiscard]] std::uint32_t Length() const;
    [[nodiscard]] int Frequency() const;
    [[nodiscard]] std::span<const std::uint8_t> Waveform() const { return waveform_; }
    [[nodiscard]] const std::filesystem::path& Path() const { return path_; }
    [[nodiscard]] bool Conflicts(const std::filesystem::path& path) const;
    [[nodiscard]] bool TakeError();
private:
    friend struct AsyncPrepare::Result;
    [[nodiscard]] bool Insert(std::filesystem::path path, WavPcmFile file, FileMode mode);
    class RecordingWriter;
    static bool Read(void* context, unsigned samples);
    static void Write(void* context, int value, unsigned samples);
    CMotherboard& board_;
    tape::State state_;
    std::optional<WavPcmFile> file_;
    std::unique_ptr<RecordingWriter> recording_;
    std::filesystem::path path_;
    bool error_ = false;
    std::array<std::uint8_t, 256> waveform_{};
};
}
