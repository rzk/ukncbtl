/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "util/IniFile.h"
#include <gtest/gtest.h>
#include <chrono>
#include <fstream>
#include <iterator>
#include <vector>

namespace
{
class IniFileTest : public testing::Test
{
protected:
    std::filesystem::path directory;
    std::filesystem::path path;

    void SetUp() override
    {
        const auto suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        directory = std::filesystem::temp_directory_path() / ("ukncbtl-ini-" + suffix);
        ASSERT_TRUE(std::filesystem::create_directory(directory));
        path = directory / std::filesystem::path(u8"\u65e5\u672c.ini");
    }
    void TearDown() override
    {
        std::error_code error;
        std::filesystem::remove(path, error);
        std::filesystem::remove(directory, error);
    }
    void Write(const std::vector<std::uint8_t>& bytes)
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(file);
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        ASSERT_TRUE(file);
    }
    std::vector<std::uint8_t> Read()
    {
        std::ifstream file(path, std::ios::binary);
        return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    }
};

TEST_F(IniFileTest, CreatesAndReopensUtf8AtUnicodePath)
{
    IniFile file;
    ASSERT_TRUE(file.Document().Set(L"UKNCBTL", L"Path", L"\u65e5\u672c/\U0001f600"));
    ASSERT_TRUE(file.Save(path));
    const auto reopened = IniFile::Open(path);
    ASSERT_TRUE(reopened);
    EXPECT_EQ(reopened->Encoding(), ini_encoding::Encoding::utf8);
    EXPECT_EQ(reopened->Document().Get(L"ukncbtl", L"path"), L"\u65e5\u672c/\U0001f600");
}

TEST_F(IniFileTest, PreservesExistingEncodingAndUnrelatedLines)
{
    for (const auto encoding : {ini_encoding::Encoding::utf8_bom, ini_encoding::Encoding::utf16_le})
    {
        const auto bytes = ini_encoding::Encode(L"; keep\r\n[UKNCBTL]\r\nPath=\u65e5\u672c\r\n[Other]\r\nx=1\r\n", encoding);
        ASSERT_TRUE(bytes);
        Write(*bytes);
        auto file = IniFile::Open(path);
        ASSERT_TRUE(file);
        ASSERT_TRUE(file->Document().Set(L"UKNCBTL", L"Sound", L"1"));
        ASSERT_TRUE(file->Save(path));
        const auto decoded = ini_encoding::Decode(Read());
        ASSERT_TRUE(decoded);
        EXPECT_EQ(decoded->encoding, encoding);
        EXPECT_EQ(decoded->text, L"; keep\r\n[UKNCBTL]\r\nPath=\u65e5\u672c\r\nSound=1\r\n[Other]\r\nx=1\r\n");
    }
}

TEST_F(IniFileTest, ReportsMissingOrUnwritablePath)
{
    const auto missing = IniFile::Open(path);
    ASSERT_FALSE(missing);
    EXPECT_EQ(missing.error(), IniFileError::open_failed);
    IniFile file;
    const auto saved = file.Save(path / "missing.ini");
    ASSERT_FALSE(saved);
    EXPECT_EQ(saved.error(), IniFileError::open_failed);
}

TEST_F(IniFileTest, RejectsMalformedAndUnsupportedFilesWithoutChangingThem)
{
    for (const auto& [bytes, error] : std::vector<std::pair<std::vector<std::uint8_t>, IniFileError>>{
             {{0xff}, IniFileError::invalid_text}, {{0xfe, 0xff, 0, 0x41}, IniFileError::unsupported_encoding}})
    {
        Write(bytes);
        const auto opened = IniFile::Open(path);
        ASSERT_FALSE(opened);
        EXPECT_EQ(opened.error(), error);
        EXPECT_EQ(Read(), bytes);
    }
}

TEST_F(IniFileTest, InvalidDocumentDoesNotTruncateExistingFile)
{
    const std::vector<std::uint8_t> original{'o', 'l', 'd'};
    Write(original);
    IniFile file;
    const std::wstring invalid(1, static_cast<wchar_t>(0xd800));
    ASSERT_TRUE(file.Document().Set(L"section", L"key", invalid));
    const auto saved = file.Save(path);
    ASSERT_FALSE(saved);
    EXPECT_EQ(saved.error(), IniFileError::invalid_text);
    EXPECT_EQ(Read(), original);
}

TEST_F(IniFileTest, RejectsOversizedInput)
{
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(file);
        file.seekp(16 * 1024 * 1024);
        file.put('x');
        ASSERT_TRUE(file);
    }
    const auto opened = IniFile::Open(path);
    ASSERT_FALSE(opened);
    EXPECT_EQ(opened.error(), IniFileError::too_large);
}

TEST_F(IniFileTest, ReplacesExistingFileAndCleansUpTemporaryFiles)
{
    Write({'o', 'l', 'd'});
    IniFile file;
    ASSERT_TRUE(file.Document().Set(L"section", L"key", L"new"));
    ASSERT_TRUE(file.Save(path));
    const auto reopened = IniFile::Open(path);
    ASSERT_TRUE(reopened);
    EXPECT_EQ(reopened->Document().Get(L"section", L"key"), L"new");
    EXPECT_EQ(std::distance(std::filesystem::directory_iterator(directory), std::filesystem::directory_iterator{}), 1);
}

TEST_F(IniFileTest, FailedReplacementLeavesDestinationAndCleansUpTemporaryFiles)
{
    ASSERT_TRUE(std::filesystem::create_directory(path));
    const auto sentinel = path / "untouched";
    { std::ofstream output(sentinel); ASSERT_TRUE(output); output << "keep"; }
    IniFile file;
    ASSERT_TRUE(file.Document().Set(L"section", L"key", L"new"));
    const auto result = file.Save(path);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), IniFileError::replace_failed);
    EXPECT_TRUE(std::filesystem::exists(sentinel));
    EXPECT_EQ(std::distance(std::filesystem::directory_iterator(directory), std::filesystem::directory_iterator{}), 1);
    EXPECT_TRUE(std::filesystem::remove(sentinel));
}
}
