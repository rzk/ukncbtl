/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "VideoDecoder.h"
#include "FrameWorker.h"
#include "emubase/Board.h"
#include "util/Profiler.h"
#include "util/ScreenScale.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <functional>
#include <optional>
#include <system_error>
#include <utility>

namespace video {
void CaptureMemory(const CMotherboard& board, MemorySnapshot& snapshot)
{
    UKNC_PROFILE_SCOPE("Video memory snapshot");
    for (int plane = 0; plane < 3; ++plane)
        std::ranges::copy(board.RAMPlane(plane), snapshot.planes[plane].begin());
}

bool UpdateMemorySnapshot(const CMotherboard& board, MemorySnapshot& snapshot)
{
    UKNC_PROFILE_SCOPE("Video memory snapshot");
    bool changed = false;
    for (int plane = 0; plane < 3; ++plane)
    {
        const auto live = board.RAMPlane(plane);
        auto& saved = snapshot.planes[plane];
        if (std::memcmp(live.data(), saved.data(), live.size_bytes()) == 0) continue;
        std::ranges::copy(live, saved.begin());
        changed = true;
    }
    return changed;
}

template<class Memory>
DisplayTag DisplayListCursor::NextFrom(const Memory& board)
{
    DisplayTag tag{address_, kind_};
    if (kind_ != TagKind::Short)
    {
        tag.tag1 = board.GetRAMWord(0, address_);
        address_ += 2;
        tag.tag2 = board.GetRAMWord(0, address_);
        address_ += 2;
    }
    tag.pixels = board.GetRAMWord(0, address_);
    address_ += 2;
    tag.link = board.GetRAMWord(0, address_);
    address_ = tag.NextAddress();
    kind_ = tag.NextKind();
    return tag;
}
DisplayTag DisplayListCursor::Next(const CMotherboard& board) { return NextFrom(board); }
DisplayTag DisplayListCursor::Next(const MemorySnapshot& snapshot) { return NextFrom(snapshot); }

namespace
{
struct Scanline
{
    std::array<uint32_t, 8> colors{};
    uint32_t cursorColor = 0;
    uint16_t pixels = 0;
    int scale = 1;
    int cursorPosition = -1;
    int cursorBit = -1;
};

template<class Memory, class Visitor>
void VisitScanlines(const Memory& board, std::span<const uint32_t> colors, Visitor visit)
{
    DisplayListCursor tags;
    uint32_t palette = 0;
    std::array<uint32_t, 8> currentPalette{};
    int scale = 1, brightness = 0;
    int cursorColor = 0, cursorPosition = 128, cursorBit = 0;
    bool cursorOn = false, graphicalCursor = false;
    for (int line = 0; line < TotalLines; ++line)
    {
        const auto tag = tags.Next(board);
        if (tag.kind != TagKind::Short)
        {
            if (tag.kind == TagKind::Palette)
                palette = tag.tag1 | (static_cast<uint32_t>(tag.tag2) << 16);
            else
            {
                const int shift = (tag.tag2 >> 4) & 3;
                scale = tag.Scale();
                brightness = (7 - (tag.tag2 & 7)) << 4;
                cursorColor = tag.tag1 & 15;
                graphicalCursor = (tag.tag1 & 16) != 0;
                cursorPosition = ((tag.tag1 >> 8) >> shift) & 0x7f;
                cursorBit = (tag.tag1 >> 5) & 7;
            }
            for (int color = 0; color < 8; ++color)
                currentPalette[color] = colors[brightness | ((palette >> (color * 4)) & 15)];
        }
        if (tag.ToggleCursor()) cursorOn = !cursorOn;
        if (line < HiddenLines) continue;
        visit(line - HiddenLines, Scanline{currentPalette, colors[cursorColor], tag.pixels, scale,
            cursorOn ? cursorPosition : -1, graphicalCursor ? cursorBit : -1});
    }
}

template<class Memory>
void RenderRow(const Memory& memory, const Scanline& line, std::span<uint32_t> row) noexcept
{
    uint16_t address = line.pixels;
    int x = 0;
    for (int position = 0; x < Width; ++position, ++address)
    {
        const auto plane0 = memory.GetRAMByte(0, address);
        const auto plane1 = memory.GetRAMByte(1, address);
        const auto plane2 = memory.GetRAMByte(2, address);
        for (int bit = 0; bit < 8; ++bit)
        {
            const int index = ((plane0 >> bit) & 1) | (((plane1 >> bit) & 1) << 1) | (((plane2 >> bit) & 1) << 2);
            const auto color = position == line.cursorPosition && (line.cursorBit < 0 || bit == line.cursorBit)
                ? line.cursorColor : line.colors[index];
            for (int repeat = 0; repeat < line.scale; ++repeat) row[x++] = color;
        }
    }
}

template<class Memory>
bool RenderRgbFrom(const Memory& board, std::span<uint32_t> pixels, std::span<const uint32_t> colors)
{
    UKNC_PROFILE_SCOPE("Video decode");
    if (pixels.size() < Width * Height || colors.size() < 128) return false;
    VisitScanlines(board, colors, [&](int row, const Scanline& line) {
        RenderRow(board, line, pixels.subspan(row * Width, Width));
    });
    return true;
}
}

struct ParallelDecoder::Worker
{
    explicit Worker(Scheduling scheduling)
        : waitUntilReady(std::move(scheduling.waitUntilReady)),
          thread([this] { return RenderRows(*job, Split, Height); }, std::move(scheduling.notify))
    {}

    static constexpr int Split = Height / 2;
    static_assert(Split % 2 == 0 && Height % 2 == 0);
    struct Job
    {
        std::reference_wrapper<const MemorySnapshot> snapshot;
        std::span<uint32_t> pixels;
        std::optional<ScaledOutput> scaled;
    };

    bool RenderRows(const Job& view, int start, int end) const noexcept
    {
        {
            UKNC_PROFILE_SCOPE("Video row decode");
            for (int row = start; row < end; ++row)
                RenderRow(view.snapshot.get(), lines[row], view.pixels.subspan(row * Width, Width));
        }
        // The scaler counts source rows from the bottom and groups pairs.
        return !view.scaled || screen_scale::ApplyBand(view.scaled->mode, view.pixels,
            view.scaled->pixels, Height - end, Height - start, view.scaled->order);
    }

    std::array<Scanline, Height> lines{};
    std::optional<Job> job;
    std::function<void(FrameWorker&)> waitUntilReady;
    FrameWorker thread;
};

ParallelDecoder::ParallelDecoder(bool enableWorker, Scheduling scheduling)
{
    if (!enableWorker) return;
    try { worker_ = std::make_unique<Worker>(std::move(scheduling)); }
    catch (const std::system_error&) {} // Fall back if the host cannot start the thread.
}

ParallelDecoder::~ParallelDecoder() = default;

bool ParallelDecoder::Render(const MemorySnapshot& snapshot, std::span<uint32_t> pixels,
    std::span<const uint32_t> colors)
{
    return Run(snapshot, pixels, colors, std::nullopt, {});
}

bool ParallelDecoder::RenderScaled(const MemorySnapshot& snapshot, std::span<uint32_t> pixels,
    std::span<const uint32_t> colors, int mode, std::span<uint32_t> scaled,
    const std::function<void()>& prepareUi, screen_scale::RowOrder order)
{
    const auto& size = screen_scale::GetMode(mode);
    if (scaled.size() < static_cast<std::size_t>(size.width) * size.height) return false;
    return Run(snapshot, pixels, colors, ScaledOutput{mode, scaled, order}, prepareUi);
}

bool ParallelDecoder::Run(const MemorySnapshot& snapshot, std::span<uint32_t> pixels,
    std::span<const uint32_t> colors, std::optional<ScaledOutput> scaled,
    const std::function<void()>& prepareUi)
{
    if (pixels.size() < Width * Height || colors.size() < 128) return false;
    const auto serial = [&] {
        return RenderRgb(snapshot, pixels, colors) &&
            (!scaled || screen_scale::Apply(scaled->mode, pixels, scaled->pixels, scaled->order));
    };
    if (!worker_)
    {
        if (prepareUi) prepareUi();
        return serial();
    }
    UKNC_PROFILE_SCOPE("Parallel video preparation");
    auto& worker = *worker_;
    VisitScanlines(snapshot, colors, [&](int row, const Scanline& line) { worker.lines[row] = line; });
    worker.job.emplace(Worker::Job{snapshot, pixels, scaled});
    if (!worker.thread.Start())
    {
        worker.job.reset();
        if (prepareUi) prepareUi();
        return serial();
    }
    bool firstBand;
    try
    {
        if (prepareUi) prepareUi();
        firstBand = worker.RenderRows(*worker.job, 0, Worker::Split);
        if (worker.waitUntilReady) worker.waitUntilReady(worker.thread);
    }
    catch (...)
    {
        (void)worker.thread.WaitOutcome();
        worker.job.reset();
        throw;
    }
    const auto outcome = worker.thread.WaitOutcome();
    worker.job.reset();
    return (firstBand && outcome == FrameWorker::Outcome::completed) || serial();
}

bool RenderRgb(const CMotherboard& board, std::span<uint32_t> pixels, std::span<const uint32_t> colors)
{ return RenderRgbFrom(board, pixels, colors); }
bool RenderRgb(const MemorySnapshot& snapshot, std::span<uint32_t> pixels, std::span<const uint32_t> colors)
{ return RenderRgbFrom(snapshot, pixels, colors); }

namespace
{
template<class Memory>
bool RenderTextFrom(const Memory& memory, std::span<uint32_t> pixels, std::span<const uint32_t> colors)
{
    if (pixels.size() < Width * Height || colors.size() < 8) return false;
    DisplayListCursor tags;
    int scale = 1;
    for (int line = 0; line < TotalLines; ++line)
    {
        const auto tag = tags.Next(memory);
        if (tag.kind == TagKind::Parameters) scale = tag.Scale();
        if (line < HiddenLines) continue;
        uint16_t address = tag.pixels;
        auto row = pixels.subspan(static_cast<std::size_t>(line - HiddenLines) * Width, Width);
        for (int x = 0; x < Width / scale; x += 8, ++address)
        {
            const auto plane0 = memory.GetRAMByte(0, address);
            const auto plane1 = memory.GetRAMByte(1, address);
            const auto plane2 = memory.GetRAMByte(2, address);
            for (int bit = 0; bit < 8; ++bit)
            {
                const int index = ((plane0 >> bit) & 1) | (((plane1 >> bit) & 1) << 1) | (((plane2 >> bit) & 1) << 2);
                row[x + bit] = colors[index];
            }
        }
    }
    return true;
}
}

bool RenderText(const CMotherboard& board, std::span<uint32_t> pixels, std::span<const uint32_t> colors)
{ return RenderTextFrom(board, pixels, colors); }
bool RenderText(const MemorySnapshot& snapshot, std::span<uint32_t> pixels, std::span<const uint32_t> colors)
{ return RenderTextFrom(snapshot, pixels, colors); }
}
