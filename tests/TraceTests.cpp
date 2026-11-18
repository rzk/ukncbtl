/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "util/Trace.h"
#include <gtest/gtest.h>
#include <string>

namespace { std::wstring captured; }
void DebugLog(const wchar_t* message) { captured = message; }

TEST(Trace, FormatsValuesAndDeliversCompleteMessageToHost)
{
    const std::wstring payload(2048, L'x');
    DebugLogFormat(L"Floppy COMMAND {:06o}: {}", 01234, payload);
    EXPECT_EQ(captured, L"Floppy COMMAND 001234: " + payload);
}
