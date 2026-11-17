/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "util/StagedFile.h"
#include <gtest/gtest.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace
{
class StagedFileTest : public testing::Test
{
protected:
    std::filesystem::path directory, destination;
    void SetUp() override
    {
        directory = std::filesystem::temp_directory_path() /
            ("ukncbtl-staged-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        ASSERT_TRUE(std::filesystem::create_directory(directory));
        destination = directory / std::filesystem::path(u8"\u65e5\u672c.bin");
    }
    void TearDown() override
    {
        std::error_code error;
        std::filesystem::remove(destination, error);
        std::filesystem::remove(directory / "occupied" / "keep", error);
        std::filesystem::remove(directory / "occupied", error);
        std::filesystem::remove(directory, error);
    }
    static void Write(const std::filesystem::path& path, std::string_view text)
    { std::ofstream file(path, std::ios::binary); ASSERT_TRUE(file.write(text.data(), static_cast<std::streamsize>(text.size()))); }
    static std::string Read(const std::filesystem::path& path)
    { std::ifstream file(path, std::ios::binary); return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()}; }
};

TEST_F(StagedFileTest, AbandonedWriteKeepsOriginalAndCleansTemporaryDirectory)
{
    Write(destination, "original");
    std::filesystem::path temporary;
    {
        auto file = staged_file::File::Create(destination); ASSERT_TRUE(file);
        temporary = file->Path();
        Write(temporary, "partial");
        EXPECT_EQ(Read(destination), "original");
    }
    EXPECT_EQ(Read(destination), "original");
    EXPECT_FALSE(std::filesystem::exists(temporary));
    EXPECT_FALSE(std::filesystem::exists(temporary.parent_path()));
}

TEST_F(StagedFileTest, CommitReplacesOriginalAndIndependentStagesDoNotCollide)
{
    Write(destination, "original");
    auto first = staged_file::File::Create(destination); ASSERT_TRUE(first);
    auto second = staged_file::File::Create(destination); ASSERT_TRUE(second);
    EXPECT_NE(first->Path(), second->Path());
    Write(first->Path(), "replacement");
    ASSERT_TRUE(first->Commit());
    EXPECT_EQ(Read(destination), "replacement");
    EXPECT_FALSE(first->Commit());
}

TEST_F(StagedFileTest, FailedReplacementPreservesExistingDirectoryAndRemovesStaging)
{
    const auto occupied = directory / "occupied";
    ASSERT_TRUE(std::filesystem::create_directory(occupied));
    Write(occupied / "keep", "original");
    std::filesystem::path temporary;
    {
        auto file = staged_file::File::Create(occupied); ASSERT_TRUE(file);
        temporary = file->Path();
        Write(temporary, "replacement");
        const auto result = file->Commit();
        ASSERT_FALSE(result);
        EXPECT_EQ(result.error(), staged_file::Error::replace_failed);
    }
    EXPECT_EQ(Read(occupied / "keep"), "original");
    EXPECT_FALSE(std::filesystem::exists(temporary.parent_path()));
    EXPECT_FALSE(staged_file::File::Create(directory / "missing" / "file"));
    EXPECT_FALSE(staged_file::File::Create({}));
}
}
