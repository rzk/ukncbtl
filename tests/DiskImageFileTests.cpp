/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "DiskImageData.h"
#include "DiskImageFile.h"
#include "AsyncDiskImage.h"
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

TEST_F(PortableDiskImageFile, AsyncCreateReservesPathAndFinishesWithExactImage)
{
    disk_image::AsyncCreate create;
    EXPECT_FALSE(create.Start({}, 80));
    ASSERT_TRUE(create.Start(path, 80));
    EXPECT_TRUE(create.Pending());
    EXPECT_EQ(create.Path(), path);
    EXPECT_FALSE(create.Start(path, 40));
    EXPECT_EQ(create.Finish(), std::optional<bool>(true));
    EXPECT_FALSE(create.Pending());
    EXPECT_TRUE(create.Path().empty());
    std::vector<std::uint8_t> expected(disk_image::Size(80));
    ASSERT_TRUE(disk_image::Build(80, expected));
    EXPECT_EQ(Read(), expected);
}

TEST_F(PortableDiskImageFile, AsyncCreateFailurePreservesFileAndAllowsRetry)
{
    ASSERT_TRUE(disk_image::CreatePortable(path, 40));
    const auto before = Read();
    disk_image::AsyncCreate create;
    ASSERT_TRUE(create.Start(path, 41));
    EXPECT_EQ(create.Finish(), std::optional<bool>(false));
    EXPECT_EQ(Read(), before);
    ASSERT_TRUE(create.Start(directory / "missing" / "disk.dsk", 80));
    EXPECT_EQ(create.Finish(), std::optional<bool>(false));
    ASSERT_TRUE(create.Start(path, 80));
    EXPECT_EQ(create.Finish(), std::optional<bool>(true));
    EXPECT_EQ(Read().size(), disk_image::Size(80));
}

TEST_F(PortableDiskImageFile, AsyncCreateDestructorCompletesOutstandingWrite)
{
    { disk_image::AsyncCreate create; ASSERT_TRUE(create.Start(path, 40)); }
    EXPECT_EQ(Read().size(), disk_image::Size(40));
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

TEST_F(PortableDiskImageFile, FailedReplacementPreservesExistingDirectory)
{
    const auto occupied = directory / "occupied";
    ASSERT_TRUE(std::filesystem::create_directory(occupied));
    { std::ofstream file(occupied / "keep"); file << "safe"; }
    const auto result = disk_image::CreatePortable(occupied, 40);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), disk_image::FileError::replace_failed);
    EXPECT_TRUE(std::filesystem::exists(occupied / "keep"));
    std::error_code error;
    std::filesystem::remove(occupied / "keep", error);
    std::filesystem::remove(occupied, error);
}

class FakeWriter final : public disk_image::FileWriter
{
public:
    bool openSucceeds = true, closeSucceeds = true, shortWrite = false;
    int opens = 0, writes = 0, closes = 0;
    std::filesystem::path openedPath;
    std::vector<std::uint8_t> bytes;

    bool Open(const std::filesystem::path& path) override
    {
        ++opens;
        openedPath = path;
        return openSucceeds;
    }
    std::size_t Write(std::span<const std::uint8_t> data) override
    {
        ++writes;
        bytes.assign(data.begin(), data.end());
        return data.size() - (shortWrite ? 1 : 0);
    }
    bool Close() override { ++closes; return closeSucceeds; }
};

TEST(PortableDiskImageErrors, InvalidArgumentsDoNotOpenWriter)
{
    FakeWriter writer;
    const auto empty = disk_image::CreatePortable({}, 40, writer);
    const auto invalid = disk_image::CreatePortable("disk.dsk", 41, writer);
    ASSERT_FALSE(empty);
    ASSERT_FALSE(invalid);
    EXPECT_EQ(empty.error(), disk_image::FileError::invalid_argument);
    EXPECT_EQ(invalid.error(), disk_image::FileError::invalid_argument);
    EXPECT_EQ(writer.opens, 0);
}

TEST(PortableDiskImageErrors, OpenFailureDoesNotWriteOrClose)
{
    FakeWriter writer;
    writer.openSucceeds = false;
    const auto result = disk_image::CreatePortable("disk.dsk", 40, writer);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), disk_image::FileError::open_failed);
    EXPECT_EQ(writer.opens, 1);
    EXPECT_EQ(writer.writes, 0);
    EXPECT_EQ(writer.closes, 0);
}

TEST(PortableDiskImageErrors, ShortWriteClosesAndTakesPrecedenceOverCloseFailure)
{
    FakeWriter writer;
    writer.shortWrite = true;
    writer.closeSucceeds = false;
    const auto result = disk_image::CreatePortable("disk.dsk", 40, writer);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), disk_image::FileError::write_failed);
    EXPECT_EQ(writer.writes, 1);
    EXPECT_EQ(writer.closes, 1);
}

TEST(PortableDiskImageErrors, CloseFailureIsReported)
{
    FakeWriter writer;
    writer.closeSucceeds = false;
    const auto result = disk_image::CreatePortable("disk.dsk", 80, writer);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), disk_image::FileError::close_failed);
    EXPECT_EQ(writer.writes, 1);
    EXPECT_EQ(writer.closes, 1);
}

TEST(PortableDiskImageErrors, SuccessfulWriteReceivesTheCompleteImage)
{
    FakeWriter writer;
    ASSERT_TRUE(disk_image::CreatePortable("disk.dsk", 80, writer));
    std::vector<std::uint8_t> expected(disk_image::Size(80));
    ASSERT_TRUE(disk_image::Build(80, expected));
    EXPECT_EQ(writer.openedPath, std::filesystem::path("disk.dsk"));
    EXPECT_EQ(writer.bytes, expected);
    EXPECT_EQ(writer.opens, 1);
    EXPECT_EQ(writer.writes, 1);
    EXPECT_EQ(writer.closes, 1);
}
}
