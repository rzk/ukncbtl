/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "Snapshot.h"
#include "InstructionHistory.h"
#include "emubase/Board.h"
#include "emubase/ImageSerialization.h"
#include <fstream>
#include <vector>

namespace snapshot {
bool Save(const std::filesystem::path& path, CMotherboard& board, uint32_t uptime)
{
    if (path.empty()) return false;
    std::vector<uint8_t> bytes(UKNCIMAGE_SIZE, 0);
    image::Writer header(std::span{bytes}.first<20>());
    header.dword(UKNCIMAGE_HEADER1);
    header.dword(UKNCIMAGE_HEADER2);
    header.dword(UKNCIMAGE_VERSION);
    header.dword(UKNCIMAGE_SIZE);
    header.dword(uptime);
    board.SaveToImage(std::span<uint8_t, UKNCIMAGE_SIZE>{bytes});

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) return false;
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    file.close();
    return !file.fail();
}

bool Save(const wchar_t* path, CMotherboard& board, uint32_t uptime)
{
    return path && Save(std::filesystem::path(path), board, uptime);
}

bool Load(const std::filesystem::path& path, CMotherboard& board, uint32_t& uptime,
          InstructionHistory& history, const std::function<void()>& stop)
{
    if (path.empty()) return false;
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    std::vector<uint8_t> bytes(UKNCIMAGE_SIZE);
    if (!file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) return false;

    image::Reader header(std::span<const uint8_t>{bytes}.first<20>());
    if (header.dword() != UKNCIMAGE_HEADER1 || header.dword() != UKNCIMAGE_HEADER2 ||
        header.dword() != UKNCIMAGE_VERSION || header.dword() != UKNCIMAGE_SIZE)
        return false;
    const auto savedUptime = header.dword();

    stop();
    board.Reset();
    board.LoadFromImage(std::span<const uint8_t, UKNCIMAGE_SIZE>{bytes});
    history.Clear();
    uptime = savedUptime;
    return true;
}

bool Load(const wchar_t* path, CMotherboard& board, uint32_t& uptime,
          InstructionHistory& history, const std::function<void()>& stop)
{
    return path && Load(std::filesystem::path(path), board, uptime, history, stop);
}
}
