/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once

#include <array>
#include <expected>
#include <filesystem>

class CMotherboard;

namespace media_slots
{
enum class Kind { floppy, hardDisk };
enum class Error { invalid_slot, empty_path, occupied, attach_failed, detach_failed };

// Slots use zero-based indexes: four floppy drives and two hard disks. This
// object exclusively manages attachments on its borrowed board.
class Slots
{
public:
    explicit Slots(CMotherboard& board) : board_(board) {}
    Slots(const Slots&) = delete;
    Slots& operator=(const Slots&) = delete;
    [[nodiscard]] std::expected<void, Error> Attach(Kind kind, int slot, const std::filesystem::path& path);
    [[nodiscard]] std::expected<void, Error> Eject(Kind kind, int slot);
    [[nodiscard]] bool Attached(Kind kind, int slot) const;
    [[nodiscard]] bool ReadOnly(Kind kind, int slot) const;
    [[nodiscard]] const std::filesystem::path* Path(Kind kind, int slot) const;
private:
    CMotherboard& board_;
    std::array<std::filesystem::path, 4> floppyPaths_{};
    std::array<std::filesystem::path, 2> hardPaths_{};
};
}
