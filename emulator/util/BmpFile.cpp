/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "BmpFile.h"
#include "LittleEndian.h"
#include <array>
#include <fstream>
#include <limits>
#include <optional>
#include <ostream>
#include <vector>

namespace
{
struct Layout { std::uint32_t stride, imageSize, fileSize; };

std::optional<Layout> GetLayout(std::span<const std::uint32_t> pixels, int width, int height)
{
    if (width <= 0 || height <= 0) return std::nullopt;
    const auto count = static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height);
    if (count > pixels.size() || count * 4 > (std::numeric_limits<std::uint32_t>::max)()) return std::nullopt;
    const auto stride = (static_cast<std::uint64_t>(width) * 3 + 3) & ~std::uint64_t{3};
    const auto imageSize = stride * static_cast<std::uint64_t>(height);
    if (imageSize > (std::numeric_limits<std::uint32_t>::max)() - 54) return std::nullopt;
    return Layout{static_cast<std::uint32_t>(stride), static_cast<std::uint32_t>(imageSize),
                  static_cast<std::uint32_t>(imageSize + 54)};
}
}

bool bmp_file::Write(std::ostream& output, std::span<const std::uint32_t> pixels, int width, int height)
{
    const auto layout = GetLayout(pixels, width, height);
    if (!layout) return false;
    std::array<std::uint8_t, 54> header{};
    header[0] = 'B'; header[1] = 'M';
    auto bytes = std::span{header};
    little_endian::write<std::uint32_t>(bytes.subspan<2, 4>(), layout->fileSize);
    little_endian::write<std::uint32_t>(bytes.subspan<10, 4>(), 54);
    little_endian::write<std::uint32_t>(bytes.subspan<14, 4>(), 40);
    little_endian::write<std::uint32_t>(bytes.subspan<18, 4>(), static_cast<std::uint32_t>(width));
    little_endian::write<std::uint32_t>(bytes.subspan<22, 4>(), static_cast<std::uint32_t>(height));
    little_endian::write<std::uint16_t>(bytes.subspan<26, 2>(), 1);
    little_endian::write<std::uint16_t>(bytes.subspan<28, 2>(), 24);
    little_endian::write<std::uint32_t>(bytes.subspan<34, 4>(), layout->imageSize);
    output.write(reinterpret_cast<const char*>(header.data()), static_cast<std::streamsize>(header.size()));
    if (!output) return false;
    std::vector<std::uint8_t> row(layout->stride, 0);
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            const auto pixel = pixels[static_cast<std::size_t>(y) * width + x];
            const auto offset = static_cast<std::size_t>(x) * 3;
            row[offset] = static_cast<std::uint8_t>(pixel);
            row[offset + 1] = static_cast<std::uint8_t>(pixel >> 8);
            row[offset + 2] = static_cast<std::uint8_t>(pixel >> 16);
        }
        output.write(reinterpret_cast<const char*>(row.data()), static_cast<std::streamsize>(row.size()));
        if (!output) return false;
    }
    return true;
}

std::expected<void, bmp_file::Error> bmp_file::Save(const std::filesystem::path& path,
    std::span<const std::uint32_t> pixels, int width, int height)
{
    if (path.empty() || !GetLayout(pixels, width, height)) return std::unexpected(Error::invalid_input);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) return std::unexpected(Error::open_failed);
    const bool written = Write(file, pixels, width, height);
    file.flush();
    const bool flushed = static_cast<bool>(file);
    file.close();
    if (!written || !flushed) return std::unexpected(Error::write_failed);
    if (!file) return std::unexpected(Error::close_failed);
    return {};
}
