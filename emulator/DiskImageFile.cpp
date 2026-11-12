/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "DiskImageFile.h"
#include "DiskImageData.h"
#include <cstdint>
#include <fstream>
#include <vector>

namespace
{
class StreamFileWriter final : public disk_image::FileWriter
{
public:
    bool Open(const std::filesystem::path& path) override
    {
        file_.open(path, std::ios::binary | std::ios::trunc);
        return static_cast<bool>(file_);
    }
    std::size_t Write(std::span<const std::uint8_t> bytes) override
    {
        file_.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        file_.flush();
        return file_ ? bytes.size() : 0;
    }
    bool Close() override
    {
        file_.close();
        return static_cast<bool>(file_);
    }
private:
    std::ofstream file_;
};
}

std::expected<void, disk_image::FileError> disk_image::CreatePortable(const std::filesystem::path& path, int tracks)
{
    StreamFileWriter writer;
    return CreatePortable(path, tracks, writer);
}

std::expected<void, disk_image::FileError> disk_image::CreatePortable(
    const std::filesystem::path& path, int tracks, FileWriter& writer)
{
    const auto size = Size(tracks);
    if (path.empty() || !size) return std::unexpected(FileError::invalid_argument);
    std::vector<std::uint8_t> image(size);
    if (!Build(tracks, image)) return std::unexpected(FileError::invalid_argument);
    if (!writer.Open(path)) return std::unexpected(FileError::open_failed);
    const auto written = writer.Write(image);
    const auto closed = writer.Close();
    if (written != image.size()) return std::unexpected(FileError::write_failed);
    if (!closed) return std::unexpected(FileError::close_failed);
    return {};
}
