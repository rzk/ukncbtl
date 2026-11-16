/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once
#include "TapeTransport.h"
#include <filesystem>
#include <optional>

class CMotherboard;
namespace preview_tape
{
// Owns the board tape callback. All operations run on the emulation thread.
class Session
{
public:
    explicit Session(CMotherboard& board) : board_(board) {}
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    [[nodiscard]] bool Open(const std::filesystem::path& path);
    [[nodiscard]] bool Create(const std::filesystem::path& path);
    [[nodiscard]] bool Close();
    [[nodiscard]] bool Play();
    void Stop();
    [[nodiscard]] bool Rewind();
    [[nodiscard]] bool Inserted() const { return state_.Inserted(); }
    [[nodiscard]] bool Playing() const { return state_.Playing(); }
    [[nodiscard]] bool Recording() const { return state_.Recording(); }
    [[nodiscard]] std::uint32_t Position() const { return file_ ? file_->Position() : 0; }
    [[nodiscard]] std::uint32_t Length() const { return file_ ? file_->Length() : 0; }
    [[nodiscard]] int Frequency() const { return file_ ? file_->Frequency() : 0; }
    [[nodiscard]] const std::filesystem::path& Path() const { return path_; }
    [[nodiscard]] bool Conflicts(const std::filesystem::path& path) const;
    [[nodiscard]] bool TakeError();
private:
    static bool Read(void* context, unsigned samples);
    static void Write(void* context, int value, unsigned samples);
    CMotherboard& board_;
    tape::State state_;
    std::optional<WavPcmFile> file_;
    std::filesystem::path path_;
    bool error_ = false;
};
}
