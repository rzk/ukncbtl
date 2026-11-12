/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>

class CMotherboard;
class InstructionHistory;

namespace snapshot {
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
