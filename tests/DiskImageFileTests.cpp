/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "DiskImageData.h"
#include "DiskImageFile.h"
#include <gtest/gtest.h>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

namespace
{
class PortableDiskImageFile : public testing::Test
{
protected:
    std::filesystem::path directory, path;
    void SetUp() override
    {
        const auto suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        directory = std::filesystem::temp_directory_path() / ("ukncbtl-disk-image-" + suffix);
        ASSERT_TRUE(std::filesystem::create_directory(directory));
        path = directory / std::filesystem::path(u8"\u65e5\u672c.dsk");
    }
    void TearDown() override
    {
        std::error_code error;
        std::filesystem::remove(path, error);
        std::filesystem::remove(directory, error);
    }
    std::vector<std::uint8_t> Read()
    {
        std::ifstream file(path, std::ios::binary);
        EXPECT_TRUE(file);
        return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    }
};

TEST_F(PortableDiskImageFile, WritesExactImagesAtUnicodePathAndTruncatesOnOverwrite)
{
    for (const auto tracks : {80, 40, 80})
    {
        ASSERT_TRUE(disk_image::CreatePortable(path, tracks));
        std::vector<std::uint8_t> expected(disk_image::Size(tracks));
        ASSERT_TRUE(disk_image::Build(tracks, expected));
        EXPECT_EQ(Read(), expected);
    }
}

TEST_F(PortableDiskImageFile, InvalidArgumentsDoNotAlterExistingFile)
{
    ASSERT_TRUE(disk_image::CreatePortable(path, 40));
    const auto before = Read();
    for (const auto tracks : {-1, 0, 39, 41, 81})
    {
        const auto result = disk_image::CreatePortable(path, tracks);
        ASSERT_FALSE(result);
        EXPECT_EQ(result.error(), disk_image::FileError::invalid_argument);
    }
    const auto empty = disk_image::CreatePortable({}, 40);
    ASSERT_FALSE(empty);
    EXPECT_EQ(empty.error(), disk_image::FileError::invalid_argument);
    EXPECT_EQ(Read(), before);
}

TEST_F(PortableDiskImageFile, InvalidDestinationReportsOpenFailure)
{
    const auto result = disk_image::CreatePortable(directory / "missing" / "image.dsk", 40);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), disk_image::FileError::open_failed);
    EXPECT_FALSE(std::filesystem::exists(path));
}
}
