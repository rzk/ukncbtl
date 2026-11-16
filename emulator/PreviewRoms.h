/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once

#include <array>
#include <expected>
#include <filesystem>

class CMotherboard;

namespace preview_roms
{
enum class Error { invalid_slot, occupied, invalid_path, open_failed, read_failed };
enum class SystemSource { embedded, file, snapshot };

// Manages system ROM and two zero-based cartridge slots on a borrowed board.
// Construction loads the embedded system ROM; callers exclusively manage the
// board's ROMs through this object while it is alive.
class Roms
{
public:
    explicit Roms(CMotherboard& board);
    Roms(const Roms&) = delete;
    Roms& operator=(const Roms&) = delete;
    void UseEmbedded();
    [[nodiscard]] std::expected<void, Error> LoadSystem(const std::filesystem::path& path);
    void MarkRestoredFromSnapshot();
    [[nodiscard]] SystemSource Source() const { return source_; }
    [[nodiscard]] const std::filesystem::path* SystemPath() const;
    [[nodiscard]] std::expected<void, Error> Insert(int slot, const std::filesystem::path& path);
    [[nodiscard]] std::expected<void, Error> Eject(int slot);
    [[nodiscard]] bool Inserted(int slot) const;
    [[nodiscard]] const std::filesystem::path* CartridgePath(int slot) const;
    [[nodiscard]] bool Conflicts(const std::filesystem::path& path) const;
private:
    CMotherboard& board_;
    SystemSource source_ = SystemSource::embedded;
    std::filesystem::path systemPath_;
    std::array<std::filesystem::path, 2> cartridgePaths_{};
};
}
