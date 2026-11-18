/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "util/SettingsStore.h"
#include <gtest/gtest.h>
#include <chrono>
#include <array>
#include <fstream>
#include <iterator>
#include <vector>

namespace
{
class SettingsStoreTest : public testing::Test
{
protected:
    std::filesystem::path directory, path;
    void SetUp() override
    {
        const auto suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        directory = std::filesystem::temp_directory_path() / ("ukncbtl-settings-" + suffix);
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

TEST_F(SettingsStoreTest, CreatesFileAndReadsOrRemovesValues)
{
    SettingsStore store(path);
    const auto absent = store.Get(L"Path");
    ASSERT_TRUE(absent);
    EXPECT_FALSE(*absent);
    ASSERT_TRUE(store.Remove(L"Missing"));
    EXPECT_FALSE(std::filesystem::exists(path));
    ASSERT_TRUE(store.Set(L"Path", L"\u65e5\u672c/\U0001f600"));
    const auto stored = store.Get(L"path");
    ASSERT_TRUE(stored);
    EXPECT_EQ(*stored, L"\u65e5\u672c/\U0001f600");
    ASSERT_TRUE(store.Remove(L"PATH"));
    const auto removed = store.Get(L"Path");
    ASSERT_TRUE(removed);
    EXPECT_FALSE(*removed);
}

TEST_F(SettingsStoreTest, EmptyValuesAreMissingAndOtherKeysAreRetained)
{
    SettingsStore store(path);
    ASSERT_TRUE(store.Set(L"Keep", L"value"));
    ASSERT_TRUE(store.Set(L"Empty", L""));
    const auto empty = store.Get(L"Empty");
    ASSERT_TRUE(empty);
    EXPECT_FALSE(*empty);
    ASSERT_TRUE(store.Remove(L"Empty"));
    const auto retained = store.Get(L"Keep");
    ASSERT_TRUE(retained);
    EXPECT_EQ(*retained, L"value");
}

TEST_F(SettingsStoreTest, ReloadsExternalEditsAndPreservesUtf16)
{
    const auto bytes = ini_encoding::Encode(L"; keep\r\n[UKNCBTL]\r\nKey=first\r\n", ini_encoding::Encoding::utf16_le);
    ASSERT_TRUE(bytes);
    Write(*bytes);
    SettingsStore store(path);
    ASSERT_EQ(store.Get(L"Key").value(), L"first");
    const auto changed = ini_encoding::Encode(L"; edited\r\n[UKNCBTL]\r\nKey=second\r\n", ini_encoding::Encoding::utf16_le);
    ASSERT_TRUE(changed);
    Write(*changed);
    ASSERT_EQ(store.Get(L"Key").value(), L"second");
    ASSERT_TRUE(store.Set(L"Other", L"3"));
    const auto decoded = ini_encoding::Decode(Read());
    ASSERT_TRUE(decoded);
    EXPECT_EQ(decoded->encoding, ini_encoding::Encoding::utf16_le);
    EXPECT_EQ(decoded->text, L"; edited\r\n[UKNCBTL]\r\nKey=second\r\nOther=3\r\n");
}

TEST_F(SettingsStoreTest, InvalidExistingFileIsNotOverwritten)
{
    const std::vector<std::uint8_t> bad{0xff};
    Write(bad);
    SettingsStore store(path);
    const auto read = store.Get(L"Key");
    ASSERT_FALSE(read);
    EXPECT_EQ(read.error(), IniFileError::invalid_text);
    const auto write = store.Set(L"Key", L"value");
    ASSERT_FALSE(write);
    EXPECT_EQ(write.error(), IniFileError::invalid_text);
    const auto remove = store.Remove(L"Key");
    ASSERT_FALSE(remove);
    EXPECT_EQ(remove.error(), IniFileError::invalid_text);
    EXPECT_EQ(Read(), bad);
}

TEST_F(SettingsStoreTest, InvalidValueAndUnwritablePathReportErrors)
{
    SettingsStore store(path);
    ASSERT_TRUE(store.Set(L"Keep", L"value"));
    const auto original = Read();
    const auto invalid = store.Set(L"Bad=key", L"value");
    ASSERT_FALSE(invalid);
    EXPECT_EQ(invalid.error(), IniFileError::invalid_entry);
    EXPECT_EQ(Read(), original);
    SettingsStore impossible(path / "settings.ini");
    const auto failed = impossible.Set(L"Key", L"value");
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error(), IniFileError::open_failed);
    EXPECT_EQ(Read(), original);
}

TEST_F(SettingsStoreTest, TypedNumbersAndColorsRoundTripAndRejectMalformedValues)
{
    SettingsStore store(path);
    ASSERT_TRUE(store.SetDword(L"Number", UINT32_MAX));
    ASSERT_EQ(store.GetDword(L"Number").value(), UINT32_MAX);
    ASSERT_TRUE(store.SetColor(L"Color", 0x00ab3412));
    ASSERT_EQ(store.GetColor(L"Color").value(), 0x00ab3412u);
    ASSERT_EQ(store.Get(L"Color").value(), L"1234AB");
    ASSERT_TRUE(store.Set(L"Number", L"4294967296"));
    EXPECT_FALSE(store.GetDword(L"Number").value());
    ASSERT_TRUE(store.Set(L"Number", std::wstring(64, L'0')));
    EXPECT_FALSE(store.GetDword(L"Number").value());
    ASSERT_TRUE(store.Set(L"Color", L"not-a-color"));
    EXPECT_FALSE(store.GetColor(L"Color").value());
    EXPECT_FALSE(store.GetDword(L"Missing").value());
}

TEST_F(SettingsStoreTest, BinaryReadsRequireExactValidDataAndDoNotModifyOnFailure)
{
    SettingsStore store(path);
    const std::array<std::uint8_t, 2> source{0x01, 0xfe};
    std::array<std::uint8_t, 2> output{};
    ASSERT_TRUE(store.SetBinary(L"Binary", source));
    ASSERT_TRUE(store.GetBinary(L"Binary", output).value());
    EXPECT_EQ(output, source);
    for (const auto text : {L"01", L"01FG", L"01FE00", L""})
    {
        ASSERT_TRUE(store.Set(L"Binary", text));
        output = {0xaa, 0xbb};
        EXPECT_FALSE(store.GetBinary(L"Binary", output).value());
        EXPECT_EQ(output, (std::array<std::uint8_t, 2>{0xaa, 0xbb}));
    }
}

TEST_F(SettingsStoreTest, TypedReadsPropagateFileErrorsWithoutChangingBinaryOutput)
{
    Write({0xff});
    SettingsStore store(path);
    std::array<std::uint8_t, 1> output{0xaa};
    EXPECT_EQ(store.GetDword(L"Key").error(), IniFileError::invalid_text);
    EXPECT_EQ(store.GetColor(L"Key").error(), IniFileError::invalid_text);
    EXPECT_EQ(store.GetBinary(L"Key", output).error(), IniFileError::invalid_text);
    EXPECT_EQ(output[0], 0xaa);
}
}
