/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "util/Profiler.h"
#include "ScreenScale.h"
#include "VideoDecoder.h"
#include <algorithm>
#include <array>
#include <cstddef>

namespace screen_scale {
namespace {
constexpr std::array<Mode, 8> modes{{
    {640, 288, L"640 x 288 Standard"},
    {640, 432, L"640 x 432 Upscaled to 1.5"},
    {640, 576, L"640 x 576 Interlaced"},
    {640, 576, L"640 x 576 Doubled"},
    {960, 576, L"960 x 576 Interlaced"},
    {960, 720, L"960 x 720, 4:3"},
    {1120, 864, L"1120 x 864 Interlaced"},
    {1280, 864, L"1280 x 864 Interlaced"},
}};

constexpr uint32_t AverageRgb(uint32_t a, uint32_t b)
{
    return ((a & 0xfefefeffu) + (b & 0xfefefeffu)) >> 1;
}

struct OutputRows
{
    struct InputBand { int begin, end, step; };

    uint32_t* bottom;
    std::ptrdiff_t stride;

    uint32_t* operator[](int bottomRow) const { return bottom + bottomRow * stride; }

    // Keep writes moving forward through the destination in either layout.
    InputBand Traversal(int first, int end, int group) const
    {
        return stride > 0 ? InputBand{first, end, group} : InputBand{end - group, first - group, -group};
    }
};

static void PrepareScreenCopy(std::span<const uint32_t> source, OutputRows destination, int first, int end)
{
    if (destination.stride < 0)
    {
        const int upperRow = video::Height - end;
        std::copy_n(source.data() + upperRow * video::Width, (end - first) * video::Width, destination[end - 1]);
        return;
    }
    for (int line = first; line < end; ++line)
    {
        const uint32_t* pSrc = source.data() + video::Width * (video::Height - 1 - line);
        std::copy_n(pSrc, video::Width, destination[line]);
    }
}

// Upscale screen from height 288 to 432
static void PrepareScreenUpscale(std::span<const uint32_t> source, OutputRows destination, int first, int end)
{
    const auto [start, limit, step] = destination.Traversal(first, end, 2);
    for (int line = start; line != limit; line += step)
    {
        const auto lower = source.subspan((video::Height - 1 - line) * video::Width, video::Width);
        const auto upper = source.subspan((video::Height - 2 - line) * video::Width, video::Width);
        const int row = line / 2 * 3;
        std::ranges::copy(lower, destination[row]);
        std::ranges::transform(lower, upper, destination[row + 1], AverageRgb);
        std::ranges::copy(upper, destination[row + 2]);
    }
}

// Upscale screen to twice height with "interlaced" effect
static void PrepareScreenUpscale2(std::span<const uint32_t> source, OutputRows destination, int first, int end)
{
    const auto [start, limit, step] = destination.Traversal(first, end, 1);
    for (int ukncline = start; ukncline != limit; ukncline += step)
    {
        const uint32_t* psrc = source.data() + (video::Height - 1 - ukncline) * video::Width;
        std::copy_n(psrc, video::Width, destination[ukncline * 2]);
        std::fill_n(destination[ukncline * 2 + 1], video::Width, 0);
    }
}

// Upscale screen to twice height
static void PrepareScreenUpscale2d(std::span<const uint32_t> source, OutputRows destination, int first, int end)
{
    const auto [start, limit, step] = destination.Traversal(first, end, 1);
    for (int ukncline = start; ukncline != limit; ukncline += step)
    {
        const uint32_t* psrc = source.data() + (video::Height - 1 - ukncline) * video::Width;
        std::copy_n(psrc, video::Width, destination[ukncline * 2]);
        std::copy_n(psrc, video::Width, destination[ukncline * 2 + 1]);
    }
}

// Upscale screen width 640->960, height 288->720
static void PrepareScreenUpscale4(std::span<const uint32_t> source, OutputRows destination, int first, int end)
{
    const auto [start, limit, step] = destination.Traversal(first, end, 2);
    for (int ukncline = start; ukncline != limit; ukncline += step)
    {
        const uint32_t* psrc1 = source.data() + (video::Height - 1 - ukncline) * video::Width;
        const uint32_t* psrc2 = psrc1 - video::Width;
        const int row = ukncline / 2 * 5;
        uint32_t* pdest0 = destination[row];
        uint32_t* pdest1 = destination[row + 1];
        uint32_t* pdest2 = destination[row + 2];
        uint32_t* pdest3 = destination[row + 3];
        uint32_t* pdest4 = destination[row + 4];
        for (int i = 0; i < video::Width / 2; i++)
        {
            uint32_t c1a = *(psrc1++);  uint32_t c1b = *(psrc1++);
            uint32_t c2a = *(psrc2++);  uint32_t c2b = *(psrc2++);
            uint32_t c1 = AverageRgb(c1a, c1b);
            uint32_t c2 = AverageRgb(c2a, c2b);
            uint32_t ca = AverageRgb(c1a, c2a);
            uint32_t cb = AverageRgb(c1b, c2b);
            uint32_t c  = AverageRgb(ca,  cb);
            (*pdest0++) = c1a;  (*pdest0++) = c1;  (*pdest0++) = c1b;
            (*pdest1++) = c1a;  (*pdest1++) = c1;  (*pdest1++) = c1b;
            (*pdest2++) = ca;   (*pdest2++) = c;   (*pdest2++) = cb;
            (*pdest3++) = c2a;  (*pdest3++) = c2;  (*pdest3++) = c2b;
            (*pdest4++) = c2a;  (*pdest4++) = c2;  (*pdest4++) = c2b;
        }
    }
}

// Upscale screen width 640->960, height 288->576 with "interlaced" effect
static void PrepareScreenUpscale3(std::span<const uint32_t> source, OutputRows destination, int first, int end)
{
    const auto [start, limit, step] = destination.Traversal(first, end, 1);
    for (int ukncline = start; ukncline != limit; ukncline += step)
    {
        const uint32_t* psrc = source.data() + (video::Height - 1 - ukncline) * video::Width;
        uint32_t* pdest = destination[ukncline * 2];
        for (int i = 0; i < video::Width / 2; ++i)
        {
            const uint32_t c1 = *psrc++;
            const uint32_t c2 = *psrc++;
            *pdest++ = c1;
            *pdest++ = AverageRgb(c1, c2);
            *pdest++ = c2;
        }
        std::fill_n(destination[ukncline * 2 + 1], 960, 0);
    }
}

// Upscale screen width 640->1120 (x1.75), height 288->864 (x3) with "interlaced" effect
static void PrepareScreenUpscale175(std::span<const uint32_t> source, OutputRows destination, int first, int end)
{
    const auto [start, limit, step] = destination.Traversal(first, end, 1);
    for (int ukncline = start; ukncline != limit; ukncline += step)
    {
        const uint32_t* psrc = source.data() + (video::Height - 1 - ukncline) * video::Width;
        uint32_t* pdest1 = destination[ukncline * 3];
        uint32_t* pdest2 = destination[ukncline * 3 + 1];
        for (int i = 0; i < video::Width / 4; i++)
        {
            uint32_t c1 = *(psrc++);
            uint32_t c2 = *(psrc++);
            uint32_t c3 = *(psrc++);
            uint32_t c4 = *(psrc++);

            *(pdest1++) = *(pdest2++) = c1;
            *(pdest1++) = *(pdest2++) = AverageRgb(c1, c2);
            *(pdest1++) = *(pdest2++) = c2;
            *(pdest1++) = *(pdest2++) = AverageRgb(c2, c3);
            *(pdest1++) = *(pdest2++) = c3;
            *(pdest1++) = *(pdest2++) = AverageRgb(c3, c4);
            *(pdest1++) = *(pdest2++) = c4;
        }
        std::fill_n(destination[ukncline * 3 + 2], 1120, 0);
    }
}

// Upscale screen width 640->1280, height 288->864 with "interlaced" effect
static void PrepareScreenUpscale5(std::span<const uint32_t> source, OutputRows destination, int first, int end)
{
    const auto [start, limit, step] = destination.Traversal(first, end, 1);
    for (int ukncline = start; ukncline != limit; ukncline += step)
    {
        const uint32_t* psrc = source.data() + (video::Height - 1 - ukncline) * video::Width;
        uint32_t* pdest = destination[ukncline * 3];
        uint32_t* pdest2 = destination[ukncline * 3 + 1];
        for (int i = 0; i < video::Width; ++i)
        {
            const uint32_t color = *psrc++;
            *pdest++ = color;
            *pdest++ = color;
            *pdest2++ = color;
            *pdest2++ = color;
        }
        std::fill_n(destination[ukncline * 3 + 2], 1280, 0);
    }
}


std::size_t ModeIndex(int mode)
{
    return mode >= 0 && static_cast<std::size_t>(mode) < modes.size() ? static_cast<std::size_t>(mode) : 1;
}
}

std::span<const Mode> Modes() { return modes; }
const Mode& GetMode(int mode) { return modes[ModeIndex(mode)]; }

bool ApplyBand(int mode, std::span<const uint32_t> source, std::span<uint32_t> destination,
    int first, int end, RowOrder order)
{
    UKNC_PROFILE_SCOPE("Screen scale");
    const std::size_t index = ModeIndex(mode);
    const auto& selected = modes[index];
    if (first < 0 || end < first || end > video::Height || first % 2 != 0 || end % 2 != 0 ||
        source.size() < static_cast<std::size_t>(video::Width) * video::Height ||
        destination.size() < static_cast<std::size_t>(selected.width) * selected.height)
        return false;
    if (first == end) return true;

    const OutputRows rows = order == RowOrder::topDown
        ? OutputRows{destination.data() + (selected.height - 1) * selected.width, -selected.width}
        : OutputRows{destination.data(), selected.width};
    switch (index)
    {
    case 0: PrepareScreenCopy(source, rows, first, end); break;
    case 1: PrepareScreenUpscale(source, rows, first, end); break;
    case 2: PrepareScreenUpscale2(source, rows, first, end); break;
    case 3: PrepareScreenUpscale2d(source, rows, first, end); break;
    case 4: PrepareScreenUpscale3(source, rows, first, end); break;
    case 5: PrepareScreenUpscale4(source, rows, first, end); break;
    case 6: PrepareScreenUpscale175(source, rows, first, end); break;
    case 7: PrepareScreenUpscale5(source, rows, first, end); break;
    }
    return true;
}

bool Apply(int mode, std::span<const uint32_t> source, std::span<uint32_t> destination, RowOrder order)
{
    return ApplyBand(mode, source, destination, 0, video::Height, order);
}
}
