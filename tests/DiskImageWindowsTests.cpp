/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "stdafx.h"
#include "DiskImageFile.h"
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

TEST(DiskImageWindows, LockedFileReportsOpenFailureAndPreservesContents)
{
    wchar_t directory[MAX_PATH]{}, temporary[MAX_PATH]{};
    ASSERT_NE(0u, GetTempPathW(MAX_PATH, directory));
    ASSERT_NE(0u, GetTempFileNameW(directory, L"dsk", 0, temporary));
    const std::filesystem::path path(temporary);
    struct Cleanup
    {
        std::filesystem::path path;
        ~Cleanup() { std::error_code error; std::filesystem::remove(path, error); }
    } cleanup{path};
    ASSERT_TRUE(disk_image::CreatePortable(path, 40));
    const auto read = [&path]
    {
        std::ifstream file(path, std::ios::binary);
        return std::vector<char>{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    };
    const auto before = read();
    ASSERT_EQ(before.size(), 409600u);
    const auto handle = CreateFileW(path.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    ASSERT_NE(INVALID_HANDLE_VALUE, handle);
    const auto result = disk_image::CreatePortable(path, 80);
    EXPECT_TRUE(CloseHandle(handle));
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), disk_image::FileError::open_failed);
    EXPECT_EQ(read(), before);
}
