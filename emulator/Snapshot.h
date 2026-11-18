/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <vector>

class CMotherboard;
class InstructionHistory;

namespace snapshot {
// Capture must run while the caller exclusively owns the board. The returned
// image can subsequently be written without accessing the board.
[[nodiscard]] std::vector<uint8_t> Capture(CMotherboard& board, uint32_t uptime);
[[nodiscard]] bool Write(const std::filesystem::path& path, std::span<const uint8_t> bytes);
// Read validates the header without accessing the board; Restore applies a
// validated image only when the caller owns the board.
[[nodiscard]] std::optional<std::vector<uint8_t>> Read(const std::filesystem::path& path);
[[nodiscard]] bool Restore(std::span<const uint8_t> bytes, CMotherboard& board, uint32_t& uptime,
                           InstructionHistory& history, const std::function<void()>& stop);
// Portable streams do not guarantee that other processes cannot write the file
// during a save or load; callers must coordinate concurrent access.
bool Save(const std::filesystem::path& path, CMotherboard& board, uint32_t uptime);
// Compatibility for the existing frontend, including null path rejection.
bool Save(const wchar_t* path, CMotherboard& board, uint32_t uptime);
// Validation completes before stop or any state mutation. stop performs the
// application's run-state/UI transition; the remaining restoration is headless.
bool Load(const std::filesystem::path& path, CMotherboard& board, uint32_t& uptime,
          InstructionHistory& history, const std::function<void()>& stop);
bool Load(const wchar_t* path, CMotherboard& board, uint32_t& uptime,
          InstructionHistory& history, const std::function<void()>& stop);
}
