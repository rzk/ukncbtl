/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once

#include "FrameTiming.h"
#include "InstructionHistory.h"
#include <cstdint>
#include <filesystem>
#include <functional>

class CMotherboard;

namespace preview_snapshot
{
// Add .uknc only when the chosen filename has no extension.
[[nodiscard]] std::filesystem::path WithDefaultExtension(std::filesystem::path path);

// Tracks emulated uptime and wraps the shared legacy-compatible snapshot format.
class Session
{
public:
    explicit Session(CMotherboard& board);
    void FrameCompleted();
    void ResetTracking();
    [[nodiscard]] std::uint32_t Uptime() const { return timing_.Uptime(); }
    [[nodiscard]] bool Save(const std::filesystem::path& path);
    // stop runs only after a snapshot has been validated, before restoration.
    [[nodiscard]] bool Load(const std::filesystem::path& path, const std::function<void()>& stop);
private:
    CMotherboard& board_;
    InstructionHistory history_;
    frame_timing::Tracker timing_;
};
}
