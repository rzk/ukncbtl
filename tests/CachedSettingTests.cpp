/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "util/CachedSetting.h"
#include <gtest/gtest.h>
#include <cstdint>
#include <optional>
#include <string>

namespace
{
struct TestStore
{
    inline static std::optional<std::uint32_t> stored;
    inline static std::wstring name;
    inline static int loads = 0;
    inline static int saves = 0;

    static bool Load(const wchar_t* key, std::uint32_t& value)
    {
        name = key;
        ++loads;
        if (!stored) return false;
        value = *stored;
        return true;
    }
    static void Save(const wchar_t* key, std::uint32_t value)
    {
        name = key;
        ++saves;
        stored = value;
    }
};

class CachedSettingTest : public testing::Test
{
    void SetUp() override
    {
        TestStore::stored.reset();
        TestStore::name.clear();
        TestStore::loads = TestStore::saves = 0;
    }
};

TEST_F(CachedSettingTest, MissingValueUsesAndCachesDefault)
{
    CachedSetting<int, TestStore> setting(L"Example", 42);
    EXPECT_EQ(setting.Get(), 42);
    TestStore::stored = 13;
    EXPECT_EQ(setting.Get(), 42);
    EXPECT_EQ(TestStore::loads, 1);
    EXPECT_EQ(TestStore::name, L"Example");
}

TEST_F(CachedSettingTest, SetPersistsAndUpdatesCacheWithoutLoading)
{
    CachedSetting<int, TestStore> setting(L"Example", 0);
    setting.Set(17);
    EXPECT_EQ(setting.Get(), 17);
    EXPECT_EQ(TestStore::stored, 17u);
    EXPECT_EQ(TestStore::name, L"Example");
    EXPECT_EQ(TestStore::loads, 0);
    EXPECT_EQ(TestStore::saves, 1);
}

TEST_F(CachedSettingTest, ResetReloadsStoredValue)
{
    TestStore::stored = 12;
    CachedSetting<int, TestStore> setting(L"Example", 0);
    EXPECT_EQ(setting.Get(), 12);
    TestStore::stored = 24;
    setting.Reset();
    EXPECT_EQ(setting.Get(), 24);
    EXPECT_EQ(TestStore::loads, 2);
}
}
