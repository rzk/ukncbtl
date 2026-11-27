/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once

#include <filesystem>
#include <string>
#include <string_view>

[[nodiscard]] inline std::filesystem::path PathFromUtf8(std::string_view text)
{
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}
