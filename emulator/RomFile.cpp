/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "RomFile.h"
#include <algorithm>
#include <fstream>
#include <limits>
#include <vector>

std::expected<void, rom_file::Error> rom_file::ReadPrefix(
    const std::filesystem::path& path, std::span<std::uint8_t> output)
{
    if (path.empty() || output.size() > static_cast<std::size_t>((std::numeric_limits<std::streamsize>::max)()))
        return std::unexpected(Error::invalid_argument);
    std::ifstream file(path, std::ios::binary);
    if (!file) return std::unexpected(Error::open_failed);
    std::vector<std::uint8_t> bytes(output.size());
    if (!bytes.empty() && !file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
        return std::unexpected(Error::read_failed);
    std::ranges::copy(bytes, output.begin());
    return {};
}
