/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
// Include the public headers without a platform precompiled header.
#include "emubase/Board.h"
#include "emubase/Processor.h"
#include <gtest/gtest.h>
#include <type_traits>

static_assert(std::is_same_v<TAPEREADCALLBACK, bool (*)(unsigned int)>);
static_assert(std::is_same_v<INSTRUCTIONCALLBACK, void (*)(const CProcessor*, uint16_t, uint16_t)>);
static_assert(std::is_same_v<decltype(std::declval<const CProcessor&>().GetName()), const wchar_t*>);

TEST(CoreHeaders, BusAddressRangeUsesFixedWidthValues)
{
    constexpr BusAddressRange range{01234, 8};
    EXPECT_EQ(range.start, 01234);
    EXPECT_EQ(range.length, 8);
}
