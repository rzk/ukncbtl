/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "util/IniDocument.h"
#include "util/IniEncoding.h"
#include "util/SettingsStore.h"
#include <Windows.h>
#include <gtest/gtest.h>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>

TEST(IniWindowsCompatibility, ReadsRepresentativeValuesLikeWindowsProfileApi)
{
    const std::wstring text =
        L"[UKNCBTL]\r\nPlain=  value  \r\nDouble=\"two words\"\r\nSingle='two words'\r\n"
        L"EmptyQuotes=\"\"\r\nDoubleSpace=\" two words \"\r\n"
        L"Different=\"hello'\r\nUnpaired=\"hello\r\nInline=value ; comment\r\n"
        L"First=one\r\nFirst=two\r\nPath=C:\\folder\\image.dsk\r\n#Hash=hash\r\n";
    const auto encoded = ini_encoding::Encode(text, ini_encoding::Encoding::utf16_le);
    ASSERT_TRUE(encoded);
    const auto suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto path = std::filesystem::temp_directory_path() / ("ukncbtl-profile-" + suffix + ".ini");
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(output);
        output.write(reinterpret_cast<const char*>(encoded->data()), static_cast<std::streamsize>(encoded->size()));
        ASSERT_TRUE(output);
    }
    const IniDocument document(text);
    for (const auto key : {L"Plain", L"Double", L"Single", L"EmptyQuotes", L"DoubleSpace", L"Different", L"Unpaired", L"Inline", L"First", L"Path", L"#Hash"})
    {
        SCOPED_TRACE(testing::Message() << key);
        std::array<wchar_t, 256> buffer{};
        GetPrivateProfileStringW(L"UKNCBTL", key, L"<missing>", buffer.data(), static_cast<DWORD>(buffer.size()), path.c_str());
        const auto value = document.Get(L"UKNCBTL", key);
        EXPECT_EQ(value.value_or(L"<missing>"), buffer.data());
    }
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, path.c_str());
    std::error_code error;
    std::filesystem::remove(path, error);
}

TEST(IniWindowsCompatibility, RemovingDuplicateKeyMatchesWindowsProfileApi)
{
    const std::wstring text = L"[UKNCBTL]\r\nKey=one\r\nKey=two\r\n[Other]\r\nKey=other\r\n";
    const auto encoded = ini_encoding::Encode(text, ini_encoding::Encoding::utf16_le);
    ASSERT_TRUE(encoded);
    const auto suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto path = std::filesystem::temp_directory_path() / ("ukncbtl-profile-delete-" + suffix + ".ini");
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(output);
        output.write(reinterpret_cast<const char*>(encoded->data()), static_cast<std::streamsize>(encoded->size()));
        ASSERT_TRUE(output);
    }
    IniDocument document(text);
    EXPECT_TRUE(document.Remove(L"ukncbtl", L"key"));
    EXPECT_TRUE(WritePrivateProfileStringW(L"UKNCBTL", L"Key", nullptr, path.c_str()));
    std::array<wchar_t, 256> buffer{};
    GetPrivateProfileStringW(L"UKNCBTL", L"Key", L"<missing>", buffer.data(), static_cast<DWORD>(buffer.size()), path.c_str());
    EXPECT_EQ(document.Get(L"UKNCBTL", L"Key").value_or(L"<missing>"), buffer.data());
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, path.c_str());
    std::error_code error;
    std::filesystem::remove(path, error);
}

TEST(IniWindowsCompatibility, PortableStoreMatchesProfileReadWriteDeleteSequence)
{
    const std::wstring text = L"; existing\r\n[UKNCBTL]\r\nPath=\"\u65e5\u672c folder\"\r\nNumber=12\r\n"
                              L"Duplicate=one\r\nDuplicate=two\r\n[Other]\r\nKey=untouched\r\n";
    const auto encoded = ini_encoding::Encode(text, ini_encoding::Encoding::utf16_le);
    ASSERT_TRUE(encoded);
    const auto suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto dir = std::filesystem::temp_directory_path() / ("ukncbtl-profile-compare-" + suffix);
    ASSERT_TRUE(std::filesystem::create_directory(dir));
    const auto legacyPath = dir / "legacy.ini";
    const auto portablePath = dir / "portable.ini";
    struct Cleanup
    {
        std::filesystem::path legacy, portable, directory;
        ~Cleanup()
        {
            WritePrivateProfileStringW(nullptr, nullptr, nullptr, legacy.c_str());
            WritePrivateProfileStringW(nullptr, nullptr, nullptr, portable.c_str());
            std::error_code error;
            std::filesystem::remove(legacy, error);
            std::filesystem::remove(portable, error);
            std::filesystem::remove(directory, error);
        }
    } cleanup{legacyPath, portablePath, dir};
    for (const auto& path : {legacyPath, portablePath})
    {
        std::ofstream output(path, std::ios::binary);
        ASSERT_TRUE(output);
        output.write(reinterpret_cast<const char*>(encoded->data()), static_cast<std::streamsize>(encoded->size()));
        ASSERT_TRUE(output);
    }

    SettingsStore store(portablePath);
    const auto compare = [&](const wchar_t* key)
    {
        SCOPED_TRACE(testing::Message() << key);
        std::array<wchar_t, 256> buffer{};
        const auto length = GetPrivateProfileStringW(L"UKNCBTL", key, nullptr, buffer.data(),
            static_cast<DWORD>(buffer.size()), legacyPath.c_str());
        const auto portable = store.Get(key);
        ASSERT_TRUE(portable);
        if (length == 0) EXPECT_FALSE(*portable);
        else { ASSERT_TRUE(*portable); EXPECT_EQ(**portable, buffer.data()); }
        std::array<wchar_t, 256> portableBuffer{};
        const auto portableLength = GetPrivateProfileStringW(L"UKNCBTL", key, nullptr, portableBuffer.data(),
            static_cast<DWORD>(portableBuffer.size()), portablePath.c_str());
        if (!*portable) EXPECT_EQ(portableLength, 0u);
        else { ASSERT_GT(portableLength, 0u); EXPECT_EQ(**portable, portableBuffer.data()); }
    };
    for (const auto key : {L"Path", L"Number", L"Duplicate", L"Missing"}) compare(key);

    for (const auto& [key, value] : std::array{
             std::pair{L"Number", L"42"}, std::pair{L"New", L"\u65e5\u672c/\u30c7\u30a3\u30b9\u30af.dsk"},
             std::pair{L"Empty", L""}, std::pair{L"Duplicate", L"changed"}})
    {
        ASSERT_TRUE(WritePrivateProfileStringW(L"UKNCBTL", key, value, legacyPath.c_str()));
        ASSERT_TRUE(store.Set(key, value));
        compare(key);
    }
    for (const auto key : {L"Number", L"Duplicate", L"Empty", L"Missing"})
    {
        ASSERT_TRUE(WritePrivateProfileStringW(L"UKNCBTL", key, nullptr, legacyPath.c_str()));
        ASSERT_TRUE(store.Remove(key));
        compare(key);
    }
    compare(L"Path");
    compare(L"New");
    ASSERT_TRUE(WritePrivateProfileStringW(L"UKNCBTL", L"External", L"external change", legacyPath.c_str()));
    ASSERT_TRUE(WritePrivateProfileStringW(L"UKNCBTL", L"External", L"external change", portablePath.c_str()));
    compare(L"External");
    const auto portable = IniFile::Open(portablePath);
    ASSERT_TRUE(portable);
    EXPECT_EQ(portable->Encoding(), ini_encoding::Encoding::utf16_le);
    EXPECT_EQ(portable->Document().Get(L"Other", L"Key"), L"untouched");
}
