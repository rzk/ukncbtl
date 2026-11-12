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
};
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
    ASSERT_TRUE(disk.AttachImage(0, path));
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
