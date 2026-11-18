/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "Snapshot.h"
#include "InstructionHistory.h"
#include "emubase/Board.h"
#include "emubase/ImageSerialization.h"
#include "util/StagedFile.h"
#include <fstream>
#include <vector>

namespace snapshot {
std::vector<uint8_t> Capture(CMotherboard& board, uint32_t uptime)
{
    std::vector<uint8_t> bytes(UKNCIMAGE_SIZE, 0);
    image::Writer header(std::span{bytes}.first<20>());
    header.dword(UKNCIMAGE_HEADER1);
    header.dword(UKNCIMAGE_HEADER2);
    header.dword(UKNCIMAGE_VERSION);
    header.dword(UKNCIMAGE_SIZE);
    header.dword(uptime);
    board.SaveToImage(std::span<uint8_t, UKNCIMAGE_SIZE>{bytes});
    return bytes;
}

bool Write(const std::filesystem::path& path, std::span<const uint8_t> bytes)
{
    if (path.empty() || bytes.size() != UKNCIMAGE_SIZE) return false;
    auto staged = staged_file::File::Create(path);
    if (!staged) return false;
    std::ofstream file(staged->Path(), std::ios::binary | std::ios::trunc);
    if (!file) return false;
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    file.close();
    return !file.fail() && static_cast<bool>(staged->Commit());
}

bool Save(const std::filesystem::path& path, CMotherboard& board, uint32_t uptime)
{
    if (path.empty()) return false;
    return Write(path, Capture(board, uptime));
}

bool Save(const wchar_t* path, CMotherboard& board, uint32_t uptime)
{
    return path && Save(std::filesystem::path(path), board, uptime);
}

std::optional<std::vector<uint8_t>> Read(const std::filesystem::path& path)
{
    if (path.empty()) return std::nullopt;
    std::ifstream file(path, std::ios::binary);
    if (!file) return std::nullopt;
    std::vector<uint8_t> bytes(UKNCIMAGE_SIZE);
    if (!file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) return std::nullopt;

    image::Reader header(std::span<const uint8_t>{bytes}.first<20>());
    if (header.dword() != UKNCIMAGE_HEADER1 || header.dword() != UKNCIMAGE_HEADER2 ||
        header.dword() != UKNCIMAGE_VERSION || header.dword() != UKNCIMAGE_SIZE)
        return std::nullopt;
    return bytes;
}

bool Restore(std::span<const uint8_t> bytes, CMotherboard& board, uint32_t& uptime,
             InstructionHistory& history, const std::function<void()>& stop)
{
    if (bytes.size() != UKNCIMAGE_SIZE) return false;
    image::Reader header(bytes.first<20>());
    if (header.dword() != UKNCIMAGE_HEADER1 || header.dword() != UKNCIMAGE_HEADER2 ||
        header.dword() != UKNCIMAGE_VERSION || header.dword() != UKNCIMAGE_SIZE)
        return false;
    const auto savedUptime = header.dword();

    stop();
    board.Reset();
    board.LoadFromImage(std::span<const uint8_t, UKNCIMAGE_SIZE>{bytes.data(), UKNCIMAGE_SIZE});
    history.Clear();
    uptime = savedUptime;
    return true;
}

bool Load(const std::filesystem::path& path, CMotherboard& board, uint32_t& uptime,
          InstructionHistory& history, const std::function<void()>& stop)
{
    const auto bytes = Read(path);
    return bytes && Restore(*bytes, board, uptime, history, stop);
}

bool Load(const wchar_t* path, CMotherboard& board, uint32_t& uptime,
          InstructionHistory& history, const std::function<void()>& stop)
{
    return path && Load(std::filesystem::path(path), board, uptime, history, stop);
}
}
