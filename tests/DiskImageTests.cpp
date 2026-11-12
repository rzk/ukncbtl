#include "stdafx.h"
#include <gtest/gtest.h>
#include "DiskImage.h"
#include <algorithm>
#include <vector>
class DiskImageFile : public testing::Test
{
protected:
    std::wstring path;
    void SetUp() override
    {
        wchar_t directory[MAX_PATH]{}, temporary[MAX_PATH]{};
        ASSERT_NE(0,GetTempPathW(MAX_PATH,directory));
        ASSERT_NE(0,GetTempFileNameW(directory,L"dsk",0,temporary));
        path = temporary;
    }
    void TearDown() override { DeleteFileW(path.c_str()); }
    std::vector<uint8_t> Read()
    {
        FILE* file = _wfopen(path.c_str(),L"rb");
        if (!file) { ADD_FAILURE(); return {}; }
        std::fseek(file,0,SEEK_END); const auto size = std::ftell(file); std::rewind(file);
        std::vector<uint8_t> bytes(static_cast<size_t>(size));
        EXPECT_EQ(bytes.size(),std::fread(bytes.data(),1,bytes.size(),file));
        EXPECT_EQ(0,std::fclose(file)); return bytes;
    }
};
TEST_F(DiskImageFile, ActualFilesMatchEntireImageAndOverwriteTruncates)
{
    for (int tracks : {80,40,80})
    {
        ASSERT_TRUE(disk_image::Create(path.c_str(),tracks));
        std::vector<uint8_t> expected(disk_image::Size(tracks));
        ASSERT_TRUE(disk_image::Build(tracks,expected));
        EXPECT_EQ(expected,Read());
    }
}
TEST_F(DiskImageFile, UnicodePath)
{
    const auto unicode = path + L"-\u65e5\u672c\u8a9e.dsk";
    ASSERT_TRUE(MoveFileW(path.c_str(),unicode.c_str())); path = unicode;
    ASSERT_TRUE(disk_image::Create(path.c_str(),40));
    EXPECT_EQ(409600,Read().size());
}
TEST_F(DiskImageFile, InvalidArgumentsDoNotTruncateExistingFile)
{
    ASSERT_TRUE(disk_image::Create(path.c_str(),40)); const auto before = Read();
    for (int tracks : {0,41,INT_MAX})
    {
        auto result = disk_image::Create(path.c_str(),tracks); ASSERT_FALSE(result);
        EXPECT_EQ(disk_image::ErrorKind::InvalidArgument,result.error().kind);
    }
    EXPECT_FALSE(disk_image::Create(nullptr,40)); EXPECT_FALSE(disk_image::Create(L"",40));
    EXPECT_EQ(before,Read());
}
TEST_F(DiskImageFile, LockedFileReportsOpenFailureAndPreservesContents)
{
    ASSERT_TRUE(disk_image::Create(path.c_str(),40)); const auto before = Read();
    const auto handle = CreateFileW(path.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr);
    ASSERT_NE(INVALID_HANDLE_VALUE,handle);
    const auto result = disk_image::Create(path.c_str(),80);
    EXPECT_TRUE(CloseHandle(handle));
    ASSERT_FALSE(result); EXPECT_EQ(disk_image::ErrorKind::Open,result.error().kind);
    EXPECT_EQ(ERROR_SHARING_VIOLATION,result.error().code); EXPECT_EQ(before,Read());
}
TEST_F(DiskImageFile, MissingParentReportsOpenFailure)
{
    auto result = disk_image::Create((path + L"\\disk.dsk").c_str(),40);
    ASSERT_FALSE(result); EXPECT_EQ(disk_image::ErrorKind::Open,result.error().kind);
}
namespace {
struct FakeDisk
{
    static FakeDisk* active;
    int opens = 0, writes = 0, closes = 0;
    bool openFail = false, writeFail = false, shortWrite = false, closeFail = false;
    DWORD error = 0;
    std::vector<uint8_t> data;
    static HANDLE WINAPI Open(LPCWSTR, DWORD access, DWORD sharing, LPSECURITY_ATTRIBUTES, DWORD disposition, DWORD, HANDLE)
    {
        ++active->opens; EXPECT_EQ(GENERIC_WRITE,access); EXPECT_EQ(FILE_SHARE_READ,sharing); EXPECT_EQ(CREATE_ALWAYS,disposition);
        active->error = ERROR_ACCESS_DENIED;
        return active->openFail ? INVALID_HANDLE_VALUE : reinterpret_cast<HANDLE>(1);
    }
    static BOOL WINAPI Write(HANDLE handle, LPCVOID bytes, DWORD count, LPDWORD written, LPOVERLAPPED overlap)
    {
        ++active->writes; EXPECT_EQ(reinterpret_cast<HANDLE>(1),handle); EXPECT_EQ(nullptr,overlap);
        const auto* begin = static_cast<const uint8_t*>(bytes); active->data.assign(begin,begin+count);
        *written = active->shortWrite ? count-1 : count; active->error = ERROR_DISK_FULL;
        return !active->writeFail;
    }
    static BOOL WINAPI Close(HANDLE handle)
    { ++active->closes; EXPECT_EQ(reinterpret_cast<HANDLE>(1),handle); active->error = ERROR_INVALID_HANDLE; return !active->closeFail; }
    static DWORD WINAPI LastError() { return active->error; }
    static disk_image::Backend Backend() { return {Open,Write,Close,LastError}; }
};
FakeDisk* FakeDisk::active = nullptr;
}
class DiskImageErrors : public testing::Test
{
protected:
    FakeDisk fake;
    void SetUp() override { FakeDisk::active = &fake; }
    void TearDown() override { FakeDisk::active = nullptr; }
};
TEST_F(DiskImageErrors, OpenFailureDoesNotWriteOrClose)
{
    fake.openFail = true; auto result = disk_image::Create(L"ignored",40,FakeDisk::Backend());
    ASSERT_FALSE(result); EXPECT_EQ(disk_image::ErrorKind::Open,result.error().kind);
    EXPECT_EQ(ERROR_ACCESS_DENIED,result.error().code); EXPECT_EQ(0,fake.writes); EXPECT_EQ(0,fake.closes);
}
TEST_F(DiskImageErrors, WriteFailureClosesAndPreservesOriginalError)
{
    fake.writeFail = true; fake.closeFail = true;
    auto result = disk_image::Create(L"ignored",40,FakeDisk::Backend());
    ASSERT_FALSE(result); EXPECT_EQ(disk_image::ErrorKind::Write,result.error().kind);
    EXPECT_EQ(ERROR_DISK_FULL,result.error().code); EXPECT_EQ(1,fake.closes);
}
TEST_F(DiskImageErrors, ShortWriteIsFailureEvenWhenApiSucceeds)
{
    fake.shortWrite = true; auto result = disk_image::Create(L"ignored",80,FakeDisk::Backend());
    ASSERT_FALSE(result); EXPECT_EQ(disk_image::ErrorKind::Write,result.error().kind);
    EXPECT_EQ(ERROR_WRITE_FAULT,result.error().code); EXPECT_EQ(1,fake.closes);
}
TEST_F(DiskImageErrors, CloseFailureIsReported)
{
    fake.closeFail = true; auto result = disk_image::Create(L"ignored",40,FakeDisk::Backend());
    ASSERT_FALSE(result); EXPECT_EQ(disk_image::ErrorKind::Close,result.error().kind);
    EXPECT_EQ(ERROR_INVALID_HANDLE,result.error().code); EXPECT_EQ(1,fake.writes); EXPECT_EQ(1,fake.closes);
}
TEST_F(DiskImageErrors, SuccessfulWriteContainsCompleteZeroFilledImage)
{
    ASSERT_TRUE(disk_image::Create(L"ignored",80,FakeDisk::Backend()));
    std::vector<uint8_t> expected(819200); ASSERT_TRUE(disk_image::Build(80,expected));
    EXPECT_EQ(expected,fake.data); EXPECT_EQ(1,fake.opens); EXPECT_EQ(1,fake.writes); EXPECT_EQ(1,fake.closes);
}
TEST_F(DiskImageErrors, IncompleteBackendRejectedBeforeOpening)
{
    for (int field = 0; field < 4; ++field)
    {
        auto backend = FakeDisk::Backend();
        switch (field) {
        case 0: backend.open = nullptr; break; case 1: backend.write = nullptr; break;
        case 2: backend.close = nullptr; break; case 3: backend.lastError = nullptr; break;
        }
        auto result = disk_image::Create(L"ignored",40,backend);
        ASSERT_FALSE(result); EXPECT_EQ(disk_image::ErrorKind::InvalidArgument,result.error().kind);
    }
    EXPECT_EQ(0,fake.opens);
}
