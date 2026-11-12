/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "DiskImageFile.h"
#include "DiskImageData.h"
#include <cstdint>
#include <fstream>
#include <vector>

std::expected<void, disk_image::FileError> disk_image::CreatePortable(const std::filesystem::path& path, int tracks)
{
    const auto size = Size(tracks);
    if (path.empty() || !size) return std::unexpected(FileError::invalid_argument);
    std::vector<std::uint8_t> image(size);
    if (!Build(tracks, image)) return std::unexpected(FileError::invalid_argument);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) return std::unexpected(FileError::open_failed);
    file.write(reinterpret_cast<const char*>(image.data()), static_cast<std::streamsize>(image.size()));
    file.flush();
    if (!file) return std::unexpected(FileError::write_failed);
    file.close();
    if (!file) return std::unexpected(FileError::close_failed);
    return {};
}
