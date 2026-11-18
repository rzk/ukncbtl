/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "emubase/Emubase.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <vector>

void DebugLog(const wchar_t*) {}

namespace {
class FloppyProbe : public CFloppyController {
public:
    auto& Raw() { return m_pDrive->data; }
    void Dirty() { m_trackchanged = true; }
    bool Changed() const { return m_trackchanged; }
    void CloseStream() { m_pDrive->fpFile->close(); }
    bool ReopenStream(const std::filesystem::path& path)
    {
        m_pDrive->fpFile->clear();
        m_pDrive->fpFile->open(path, std::ios::binary | std::ios::in | std::ios::out);
        return static_cast<bool>(*m_pDrive->fpFile);
    }
};
}

TEST(FloppyPortable, FailedFlushRetainsDirtyTrackAndReportsUntilRetried)
{
    const auto path = std::filesystem::temp_directory_path() /
        ("ukncbtl-floppy-error-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".dsk");
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove(path, ec); } } cleanup{path};
    { std::ofstream file(path, std::ios::binary); const std::vector<char> bytes(5120, 0); ASSERT_TRUE(file.write(bytes.data(), bytes.size())); }
    FloppyProbe disk;
    ASSERT_TRUE(disk.AttachImage(0, path));
    const auto goodTrack = disk.Raw();
    disk.Raw().fill(0);
    disk.Dirty();
    auto prepared = CFloppyController::OpenImage(path, true);
    ASSERT_TRUE(prepared);
    EXPECT_FALSE(disk.AttachPreparedImage(1, std::move(*prepared)));
    EXPECT_FALSE(disk.IsAttached(1));
    EXPECT_TRUE(disk.Changed());
    EXPECT_TRUE(disk.IsAttached(0));
    const auto invalid = disk.TakeFailure();
    ASSERT_TRUE(invalid);
    EXPECT_EQ(invalid->error, FloppyError::invalidTrack);
    EXPECT_EQ(invalid->slot, 0);
    EXPECT_FALSE(disk.TakeFailure());
    disk.Raw() = goodTrack;
    disk.CloseStream();
    EXPECT_FALSE(disk.Flush());
    EXPECT_TRUE(disk.Changed());
    const auto write = disk.TakeFailure();
    ASSERT_TRUE(write);
    EXPECT_EQ(write->error, FloppyError::writeFailed);
    ASSERT_TRUE(disk.ReopenStream(path));
    EXPECT_TRUE(disk.Flush());
    EXPECT_FALSE(disk.Changed());
    EXPECT_FALSE(disk.TakeFailure());
}

TEST(FloppyPortable, ReadFailureReportsAndProtectsTheDrive)
{
    const auto path = std::filesystem::temp_directory_path() /
        ("ukncbtl-floppy-read-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".dsk");
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove(path, ec); } } cleanup{path};
    { std::ofstream file(path, std::ios::binary); const std::vector<char> bytes(5120, 0); ASSERT_TRUE(file.write(bytes.data(), bytes.size())); }
    FloppyProbe disk;
    ASSERT_TRUE(disk.AttachImage(0, path));
    disk.CloseStream();
    disk.Reset();
    const auto failure = disk.TakeFailure();
    ASSERT_TRUE(failure);
    EXPECT_EQ(failure->error, FloppyError::readFailed);
    EXPECT_EQ(failure->slot, 0);
    EXPECT_TRUE(disk.IsReadOnly(0));
}

TEST(FloppyPortable, ExplicitReadOnlyMountProtectsAFileThatIsWritable)
{
    const auto path = std::filesystem::temp_directory_path() /
        ("ukncbtl-floppy-readonly-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".dsk");
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove(path, ec); } } cleanup{path};
    const std::vector<char> original(5120, 0x35);
    { std::ofstream file(path, std::ios::binary); ASSERT_TRUE(file.write(original.data(), original.size())); }
    FloppyProbe disk;
    ASSERT_TRUE(disk.AttachImage(0, path, true));
    EXPECT_TRUE(disk.IsReadOnly(0));
    disk.SetCommand(02003 | FLOPPY_CMD_ENGINESTART);
    EXPECT_NE(disk.GetState() & FLOPPY_STATUS_WRITEPROTECT, 0);
    disk.WriteData(0x1234);
    for (int index = 0; index < 100; ++index) disk.Periodic();
    EXPECT_FALSE(disk.Changed());
    disk.DetachImage(0);
    std::ifstream file(path, std::ios::binary);
    const std::vector<char> bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    EXPECT_EQ(bytes, original);
}

TEST(FloppyPortable, UnicodeRtdImagePreservesHeaderAndWritesTrack)
{
    const auto suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    auto path = std::filesystem::temp_directory_path() / ("ukncbtl-floppy-" + suffix);
    path += std::filesystem::path(u8"-\u65e5\u672c.RTD");
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() { std::error_code error; std::filesystem::remove(path, error); }
    } cleanup{path};

    std::vector<uint8_t> image(256 + 5120, 0x35);
    std::fill_n(image.begin(), 256, 0xa5);
    std::ofstream output(path, std::ios::binary);
    ASSERT_TRUE(output.write(reinterpret_cast<const char*>(image.data()), image.size()));
    output.close();
    ASSERT_TRUE(output);

    FloppyProbe disk;
    ASSERT_FALSE(disk.AttachImage(0, std::filesystem::path{}));
    ASSERT_FALSE(disk.AttachImage(-1, path));
    auto prepared = CFloppyController::OpenImage(path, false);
    ASSERT_TRUE(prepared);
    ASSERT_TRUE(disk.AttachPreparedImage(0, std::move(*prepared)));
    disk.SetCommand(02003 | FLOPPY_CMD_ENGINESTART);
    ASSERT_EQ(disk.Raw()[96], 0x35);
    disk.Raw()[96] = 0x7a;
    disk.Dirty();
    disk.Reset();
    ASSERT_FALSE(disk.Changed());
    disk.DetachImage(0);

    std::ifstream input(path, std::ios::binary);
    std::vector<uint8_t> actual(image.size());
    ASSERT_TRUE(input.read(reinterpret_cast<char*>(actual.data()), actual.size()));
    image[256] = 0x7a;
    EXPECT_EQ(actual, image);
}
