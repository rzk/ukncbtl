/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once
#include "PrinterOutput.h"
#include <array>
#include <filesystem>
#include <memory>

class CMotherboard;
class FrameWorker;
namespace preview_printer
{
// The borrowed board must outlive this session. The callback runs during a
// worker-owned frame; session operations must be serialized with the handoff.
class Session
{
public:
    explicit Session(CMotherboard& board);
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    [[nodiscard]] bool Start(const std::filesystem::path& path);
    [[nodiscard]] bool Stop();
    [[nodiscard]] bool Flush();
    [[nodiscard]] bool Check();
    // Restore the host device's online bit after a machine reset or state load.
    void Synchronize();
    [[nodiscard]] bool Active() const { return writer_ != nullptr; }
    [[nodiscard]] bool Conflicts(const std::filesystem::path& path) const;
    [[nodiscard]] const std::filesystem::path& Path() const { return path_; }
private:
    static bool Write(void* context, std::uint8_t value);
    bool QueueByte(std::uint8_t value);
    void Submit();
    void Collect(bool wait);
    void Drain();
    static constexpr std::size_t BatchSize = 4096;
    CMotherboard& board_;
    PrinterOutput output_;
    std::filesystem::path path_;
    std::array<std::uint8_t, BatchSize> queued_{}, writing_{};
    std::size_t queuedSize_ = 0, writingSize_ = 0;
    bool pending_ = false, failed_ = false;
    std::unique_ptr<FrameWorker> writer_;
};
}
