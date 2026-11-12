/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "emubase/Emubase.h"
#include <gtest/gtest.h>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <vector>

TEST(HardPortable, UnicodeImageSupportsSectorWriteAndRead)
{
    const auto suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    auto path = std::filesystem::temp_directory_path() / ("ukncbtl-hard-" + suffix);
    path += std::filesystem::path(u8"-\u65e5\u672c.img");
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() { std::error_code error; std::filesystem::remove(path, error); }
    } cleanup{path};

    std::vector<uint8_t> image(64 * 512, 0);
    image[0] = 8; image[1] = 2;
    std::ofstream output(path, std::ios::binary);
    ASSERT_TRUE(output.write(reinterpret_cast<const char*>(image.data()), image.size()));
    output.close();
    ASSERT_TRUE(output);

    CHardDrive disk;
    ASSERT_FALSE(disk.AttachImage(std::filesystem::path{}));
    ASSERT_TRUE(disk.AttachImage(path));
    disk.Reset();
    for (int i = 0; i < 2; ++i) disk.Periodic();
    const auto selectSecondSector = [&] {
        disk.WritePort(0x1f4, 0); disk.WritePort(0x1f5, 0);
        disk.WritePort(0x1f6, 0); disk.WritePort(0x1f3, 2); disk.WritePort(0x1f2, 1);
    };
    selectSecondSector();
    disk.WritePort(0x1f7, 0x30);
    for (int i = 0; i < 256; ++i) disk.WritePort(0x1f0, 0x1234);
    for (int i = 0; i < 256; ++i) disk.Periodic();
    ASSERT_EQ(disk.ReadPort(0x1f7) & 0x89, 0);

    selectSecondSector();
    disk.WritePort(0x1f7, 0x20);
    for (int i = 0; i < 768; ++i) disk.Periodic();
    ASSERT_EQ(disk.ReadPort(0x1f7) & 0x89, 8);
    for (int i = 0; i < 256; ++i) EXPECT_EQ(disk.ReadPort(0x1f0), 0x1234);
    disk.DetachImage();
}
