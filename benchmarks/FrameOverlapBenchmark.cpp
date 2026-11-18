/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "Workloads.h"
#include "FrameWorker.h"
#include "ScreenshotImage.h"
#include "VideoDecoder.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <utility>
#include <vector>

void DebugLog(const wchar_t*) {}

namespace
{
constexpr int Frames = 200;
constexpr int Repetitions = 5;
using Clock = std::chrono::steady_clock;

std::pair<double, std::uint32_t> Measure(bool parallel)
{
    CMotherboard board;
    workloads::Prepare(board);
    board.SetRAMWord(0, 0000270, 0x8000);
    board.SetRAMWord(0, 0000272, 0x7006);
    board.SetRAMWord(0, 0x7000, 0x3210);
    board.SetRAMWord(0, 0x7002, 0x7654);
    board.SetRAMWord(0, 0x7004, 0x8000);
    board.SetRAMWord(0, 0x7006, 0x7008);
    board.SetRAMWord(0, 0x7008, 0x8000);
    board.SetRAMWord(0, 0x700a, 0x7008);
    for (int offset = 0; offset < 80; ++offset)
        for (int plane = 0; plane < 3; ++plane)
            board.SetRAMByte(plane, static_cast<uint16_t>(0x8000 + offset),
                static_cast<uint8_t>((offset * 37 + plane * 71) & 0xff));
    video::MemorySnapshot snapshot;
    std::vector<uint32_t> pixels(video::Width * video::Height);
    std::array<uint32_t, 128> colors{};
    for (unsigned i = 0; i < colors.size(); ++i) colors[i] = i * 0x10101;
    FrameWorker worker([&] { return board.SystemFrame(); });
    std::uint32_t checksum = 2166136261u;
    const auto start = Clock::now();
    for (int frame = 0; frame < Frames; ++frame)
    {
        if (parallel)
        {
            video::CaptureMemory(board, snapshot);
            if (!worker.Start()) return {-1, 0};
        }
        if (!(parallel ? video::RenderRgb(snapshot, pixels, colors) : video::RenderRgb(board, pixels, colors)))
            return {-1, 0};
        const auto image = screenshot_image::Prepare(pixels, 1);
        if (!image) return {-1, 0};
        checksum = checksum * 16777619u ^ image->pixels[static_cast<std::size_t>(frame) % image->pixels.size()];
        if (parallel ? !worker.Wait() : !board.SystemFrame()) return {-1, 0};
    }
    const auto elapsed = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    return {elapsed / Frames, checksum};
}
}

int main()
{
    CProcessor::Init();
    std::array<double, Repetitions> serial{}, overlap{};
    for (int i = 0; i < Repetitions; ++i)
    {
        std::pair<double, std::uint32_t> serialRun, overlapRun;
        // Alternate order to reduce bias from warm-up and scheduling.
        if (i % 2 == 0) { serialRun = Measure(false); overlapRun = Measure(true); }
        else { overlapRun = Measure(true); serialRun = Measure(false); }
        if (serialRun.first < 0 || overlapRun.first < 0 || serialRun.second != overlapRun.second)
        { std::fprintf(stderr, "Frame workload failed or produced different video output.\n"); CProcessor::Done(); return 1; }
        serial[i] = serialRun.first;
        overlap[i] = overlapRun.first;
    }
    std::ranges::sort(serial);
    std::ranges::sort(overlap);
    std::printf("Median milliseconds per synthetic frame: serial %.3f, overlap %.3f\n",
        serial[Repetitions / 2], overlap[Repetitions / 2]);
    CProcessor::Done();
}
