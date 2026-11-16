/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "PreviewRoms.h"
#include "EmbeddedRom.h"
#include "RomFile.h"
#include "emubase/Board.h"
#include <algorithm>
#include <cstdint>
#include <utility>

namespace
{
preview_roms::Error Convert(rom_file::Error error)
{
    switch (error)
    {
    case rom_file::Error::invalid_argument: return preview_roms::Error::invalid_path;
    case rom_file::Error::open_failed: return preview_roms::Error::open_failed;
    case rom_file::Error::read_failed: return preview_roms::Error::read_failed;
    }
    return preview_roms::Error::read_failed;
}

bool SameFile(const std::filesystem::path& first, const std::filesystem::path& second)
{
    if (first.empty() || second.empty()) return false;
    if (first == second) return true;
    std::error_code error;
    return std::filesystem::equivalent(first, second, error) && !error;
}
}

preview_roms::Roms::Roms(CMotherboard& board) : board_(board) { UseEmbedded(); }

void preview_roms::Roms::UseEmbedded()
{
    std::array<std::uint8_t, 32768> image{};
    std::ranges::copy(embedded_rom::Bytes(), image.begin());
    board_.LoadROM(image);
    systemPath_.clear();
    source_ = SystemSource::embedded;
}

std::expected<void, preview_roms::Error> preview_roms::Roms::LoadSystem(const std::filesystem::path& path)
{
    auto savedPath = path;
    std::array<std::uint8_t, 32768> image{};
    const auto loaded = rom_file::ReadPrefix(path, std::span{image}.first(embedded_rom::Size));
    if (!loaded) return std::unexpected(Convert(loaded.error()));
    board_.LoadROM(image);
    systemPath_ = std::move(savedPath);
    source_ = SystemSource::file;
    return {};
}

void preview_roms::Roms::MarkRestoredFromSnapshot()
{
    systemPath_.clear();
    source_ = SystemSource::snapshot;
}

const std::filesystem::path* preview_roms::Roms::SystemPath() const
{
    return source_ == SystemSource::file ? &systemPath_ : nullptr;
}

bool preview_roms::Roms::Inserted(int slot) const
{
    return slot >= 0 && slot < 2 && board_.IsROMCartridgeLoaded(slot + 1);
}

const std::filesystem::path* preview_roms::Roms::CartridgePath(int slot) const
{
    return Inserted(slot) ? &cartridgePaths_[slot] : nullptr;
}

std::expected<void, preview_roms::Error> preview_roms::Roms::Insert(int slot, const std::filesystem::path& path)
{
    if (slot < 0 || slot >= 2) return std::unexpected(Error::invalid_slot);
    if (Inserted(slot)) return std::unexpected(Error::occupied);
    auto savedPath = path;
    std::array<std::uint8_t, 24 * 1024> image{};
    const auto loaded = rom_file::ReadPrefix(path, image);
    if (!loaded) return std::unexpected(Convert(loaded.error()));
    board_.LoadROMCartridge(slot + 1, image);
    cartridgePaths_[slot] = std::move(savedPath);
    return {};
}

std::expected<void, preview_roms::Error> preview_roms::Roms::Eject(int slot)
{
    if (slot < 0 || slot >= 2) return std::unexpected(Error::invalid_slot);
    board_.UnloadROMCartridge(slot + 1);
    cartridgePaths_[slot].clear();
    return {};
}

bool preview_roms::Roms::Conflicts(const std::filesystem::path& path) const
{
    if (const auto* system = SystemPath(); system && SameFile(*system, path)) return true;
    for (int slot = 0; slot < 2; ++slot)
        if (const auto* cartridge = CartridgePath(slot); cartridge && SameFile(*cartridge, path)) return true;
    return false;
}
