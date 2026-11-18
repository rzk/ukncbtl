/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once

#include <array>
#include <expected>
#include <filesystem>
#include <cstdint>

class CMotherboard;

namespace preview_roms
{
enum class Error { invalid_slot, occupied, invalid_path, open_failed, read_failed, hard_disk_attached };
enum class SystemSource { embedded, file, snapshot };
using SystemImage = std::array<std::uint8_t, 32768>;
using CartridgeImage = std::array<std::uint8_t, 24 * 1024>;
// These reads do not access the board and may run on a worker.
[[nodiscard]] std::expected<SystemImage, Error> ReadSystem(const std::filesystem::path& path);
[[nodiscard]] std::expected<CartridgeImage, Error> ReadCartridge(const std::filesystem::path& path);

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
    void InstallSystem(std::filesystem::path path, const SystemImage& image);
    void MarkRestoredFromSnapshot();
    [[nodiscard]] SystemSource Source() const { return source_; }
    [[nodiscard]] const std::filesystem::path* SystemPath() const;
    [[nodiscard]] std::expected<void, Error> Insert(int slot, const std::filesystem::path& path);
    [[nodiscard]] std::expected<void, Error> InstallCartridge(int slot, std::filesystem::path path, const CartridgeImage& image);
    // Eject the matching hard disk before removing its controller cartridge.
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
