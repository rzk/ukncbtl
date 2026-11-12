/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once

#include <format>
#include <utility>

// The host provides the sink; core code only depends on this portable contract.
// The message is valid for the duration of the call.
void DebugLog(const wchar_t* message);

template<class... Args>
void DebugLogFormat(std::wformat_string<Args...> format, Args&&... args)
{
    DebugLog(std::format(format, std::forward<Args>(args)...).c_str());
}
