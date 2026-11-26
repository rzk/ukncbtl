/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once

#include <array>
#include <expected>
#include <filesystem>
#include <memory>

class CMotherboard;
class CHardDrive;
struct PreparedFloppyImage;

namespace media_slots
{
enum class Kind { floppy, hardDisk };
enum class Error { invalid_slot, empty_path, occupied, already_mounted, missing_cartridge, attach_failed, detach_failed };

// Slots use zero-based indexes: four floppy drives and two hard disks. This
// object exclusively manages attachments on its borrowed board. A hard disk
// requires a cartridge in the matching slot, as in the legacy frontend.
class Slots
{
public:
    explicit Slots(CMotherboard& board) : board_(board) {}
    Slots(const Slots&) = delete;
    Slots& operator=(const Slots&) = delete;
    [[nodiscard]] std::expected<void, Error> Attach(Kind kind, int slot, const std::filesystem::path& path, bool readOnly = false);
    [[nodiscard]] std::expected<void, Error> CheckAttach(Kind kind, int slot, const std::filesystem::path& path, bool readOnly) const;
    [[nodiscard]] std::expected<void, Error> Eject(Kind kind, int slot);
    [[nodiscard]] bool FlushFloppy();
    [[nodiscard]] bool Attached(Kind kind, int slot) const;
    [[nodiscard]] bool ReadOnly(Kind kind, int slot) const;
    // Distinguishes an explicit read-only choice from a filesystem fallback.
    [[nodiscard]] bool RequestedReadOnly(Kind kind, int slot) const;
    [[nodiscard]] const std::filesystem::path* Path(Kind kind, int slot) const;
    // Detect existing aliases (including hard links) to mounted disk images.
    [[nodiscard]] bool Conflicts(const std::filesystem::path& path) const;
private:
    friend class PreparedDisk;
    [[nodiscard]] std::expected<void, Error> InstallFloppy(int slot, std::filesystem::path path, bool readOnly,
        PreparedFloppyImage image);
    [[nodiscard]] std::expected<void, Error> InstallHardDisk(int slot, std::filesystem::path path, bool readOnly,
        std::unique_ptr<CHardDrive> drive);
    CMotherboard& board_;
    std::array<std::filesystem::path, 4> floppyPaths_{};
    std::array<std::filesystem::path, 2> hardPaths_{};
    std::array<bool, 4> floppyReadOnly_{};
    std::array<bool, 2> hardReadOnly_{};
};
}
