/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "ScreenshotFormat.h"
#include <string_view>

screenshot_format::Format screenshot_format::FromPath(const std::filesystem::path& path) noexcept
{
    using Char = std::filesystem::path::value_type;
    const std::basic_string_view<Char> name(path.native());
    if (name.size() < 4) return Format::png;
    const auto suffix = name.substr(name.size() - 4);
    const auto isLetter = [](Char value, char lower) {
        return value == static_cast<Char>(lower) || value == static_cast<Char>(lower - 'a' + 'A');
    };
    return suffix[0] == static_cast<Char>('.') && isLetter(suffix[1], 'b') &&
        isLetter(suffix[2], 'm') && isLetter(suffix[3], 'p') ? Format::bmp : Format::png;
}
