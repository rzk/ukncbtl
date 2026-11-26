/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once

#include <cstdint>
#include <span>

namespace screen_scale {
enum class RowOrder { bottomUp, topDown };

struct Mode
{
    int width;
    int height;
    const wchar_t* description;
};

[[nodiscard]] std::span<const Mode> Modes();
// Out-of-range mode numbers use the default 640 x 432 mode.
[[nodiscard]] const Mode& GetMode(int mode);
// Source rows are top-down. The output defaults to screenshot row order.
[[nodiscard]] bool Apply(int mode, std::span<const uint32_t> source, std::span<uint32_t> destination,
    RowOrder order = RowOrder::bottomUp);
// Scale an even-aligned range of source rows, counted from the bottom.
// Only the corresponding destination rows are written. Concurrent calls
// must use disjoint ranges and separate source and destination storage.
[[nodiscard]] bool ApplyBand(int mode, std::span<const uint32_t> source, std::span<uint32_t> destination,
    int firstBottomRow, int endBottomRow, RowOrder order = RowOrder::bottomUp);
}
