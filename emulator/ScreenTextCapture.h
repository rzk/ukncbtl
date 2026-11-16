/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once
#include <optional>
#include <string>

class CMotherboard;
namespace screen_text
{
// Capture the text display and machine fonts without consuming device registers.
[[nodiscard]] std::optional<std::string> Capture(CMotherboard& board);
}
