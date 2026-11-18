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
    auto image = ReadSystem(path);
    if (!image) return std::unexpected(image.error());
    InstallSystem(path, *image);
    return {};
}

std::expected<preview_roms::SystemImage, preview_roms::Error> preview_roms::ReadSystem(const std::filesystem::path& path)
{
    SystemImage image{};
    const auto loaded = rom_file::ReadPrefix(path, std::span{image}.first(embedded_rom::Size));
    if (!loaded) return std::unexpected(Convert(loaded.error()));
    return image;
}

void preview_roms::Roms::InstallSystem(std::filesystem::path path, const SystemImage& image)
{
    board_.LoadROM(image);
    systemPath_ = std::move(path);
    source_ = SystemSource::file;
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
    auto image = ReadCartridge(path);
    if (!image) return std::unexpected(image.error());
    return InstallCartridge(slot, path, *image);
}

std::expected<preview_roms::CartridgeImage, preview_roms::Error> preview_roms::ReadCartridge(const std::filesystem::path& path)
{
    CartridgeImage image{};
    const auto loaded = rom_file::ReadPrefix(path, image);
    if (!loaded) return std::unexpected(Convert(loaded.error()));
    return image;
}

std::expected<void, preview_roms::Error> preview_roms::Roms::InstallCartridge(int slot, std::filesystem::path path, const CartridgeImage& image)
{
    if (slot < 0 || slot >= 2) return std::unexpected(Error::invalid_slot);
    if (Inserted(slot)) return std::unexpected(Error::occupied);
    board_.LoadROMCartridge(slot + 1, image);
    cartridgePaths_[slot] = std::move(path);
    return {};
}

std::expected<void, preview_roms::Error> preview_roms::Roms::Eject(int slot)
{
    if (slot < 0 || slot >= 2) return std::unexpected(Error::invalid_slot);
    if (board_.IsHardImageAttached(slot + 1)) return std::unexpected(Error::hard_disk_attached);
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
