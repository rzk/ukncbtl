/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "Workloads.h"
#include "FrameWorker.h"
#include "VideoDecoder.h"
#include "util/ScreenScale.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>

void DebugLog(const wchar_t*) {}

namespace
{
constexpr int Frames = 200;
constexpr int Repetitions = 5;
using Clock = std::chrono::steady_clock;
enum class Mode { serial, overlap, parallelRows };

struct Result
{
    double frameMs = -1;
    double prepareMs = 0;
    std::uint32_t checksum = 0;
};

void PrepareVideo(CMotherboard& board)
{
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
}

Result Measure(Mode mode, int displayMode)
{
    CMotherboard board;
    workloads::Prepare(board);
    PrepareVideo(board);
    video::MemorySnapshot snapshot;
    std::vector<uint32_t> pixels(video::Width * video::Height);
    const auto& size = screen_scale::GetMode(displayMode);
    std::vector<uint32_t> scaled(static_cast<std::size_t>(size.width) * size.height);
    std::array<uint32_t, 128> colors{};
    for (unsigned i = 0; i < colors.size(); ++i) colors[i] = i * 0x10101;
    FrameWorker worker([&] { return board.SystemFrame(); });
    video::ParallelDecoder videoWorker(mode == Mode::parallelRows);
    const bool overlap = mode != Mode::serial;
    std::uint32_t checksum = 2166136261u;
    Clock::duration preparationTime{};
    const auto start = Clock::now();
    for (int frame = 0; frame < Frames; ++frame)
    {
        if (overlap)
        {
            video::CaptureMemory(board, snapshot);
            if (!worker.Start()) return {};
        }
        const auto prepareStarted = Clock::now();
        bool prepared;
        if (mode == Mode::parallelRows)
            prepared = videoWorker.RenderScaled(snapshot, pixels, colors, displayMode, scaled);
        else
            prepared = (overlap ? video::RenderRgb(snapshot, pixels, colors) : video::RenderRgb(board, pixels, colors)) &&
                screen_scale::Apply(displayMode, pixels, scaled);
        preparationTime += Clock::now() - prepareStarted;
        if (!prepared) return {};
        checksum = checksum * 16777619u ^ scaled[(static_cast<std::size_t>(frame) * 3433) % scaled.size()];
        if (overlap ? !worker.Wait() : !board.SystemFrame()) return {};
    }
    const auto elapsed = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    return {elapsed / Frames, std::chrono::duration<double, std::milli>(preparationTime).count() / Frames, checksum};
}

bool BenchmarkMode(int displayMode)
{
    std::array<std::array<double, Repetitions>, 3> timings{};
    auto prepareTimings = timings;
    constexpr std::array modes{Mode::serial, Mode::overlap, Mode::parallelRows};
    for (int i = 0; i < Repetitions; ++i)
    {
        std::array<std::uint32_t, modes.size()> checksums{};
        for (std::size_t offset = 0; offset < modes.size(); ++offset)
        {
            const auto index = (i + offset) % modes.size();
            const auto result = Measure(modes[index], displayMode);
            if (result.frameMs < 0)
            { std::fprintf(stderr, "Frame workload failed.\n"); return false; }
            timings[index][i] = result.frameMs;
            prepareTimings[index][i] = result.prepareMs;
            checksums[index] = result.checksum;
        }
        if (checksums[0] != checksums[1] || checksums[0] != checksums[2])
        { std::fprintf(stderr, "Frame workload produced different video output.\n"); return false; }
    }
    for (auto& times : timings) std::ranges::sort(times);
    for (auto& times : prepareTimings) std::ranges::sort(times);
    std::printf("Mode %d median ms per synthetic frame: serial %.3f, overlap %.3f, parallel bands %.3f\n",
        displayMode, timings[0][Repetitions / 2], timings[1][Repetitions / 2], timings[2][Repetitions / 2]);
    std::printf("Mode %d median ms preparing video: serial %.3f, overlap %.3f, parallel bands %.3f\n",
        displayMode, prepareTimings[0][Repetitions / 2], prepareTimings[1][Repetitions / 2], prepareTimings[2][Repetitions / 2]);
    return true;
}

bool BenchmarkIdle(int displayMode)
{
    CMotherboard board;
    PrepareVideo(board);
    const auto& size = screen_scale::GetMode(displayMode);
    std::vector<uint32_t> raw(video::Width * video::Height);
    std::vector<uint32_t> expected(static_cast<std::size_t>(size.width) * size.height), scaled(expected.size());
    std::array<uint32_t, 128> colors{};
    for (unsigned i = 0; i < colors.size(); ++i) colors[i] = i * 0x10101;

    video::MemorySnapshot snapshot;
    video::ParallelDecoder decoder;
    if (!video::UpdateMemorySnapshot(board, snapshot) || !decoder.RenderScaled(snapshot, raw, colors, displayMode, scaled))
        return false;
    std::array<double, Repetitions> redrawTimes{}, reuseTimes{};
    for (int i = 0; i < Repetitions; ++i)
    {
        auto start = Clock::now();
        for (int frame = 0; frame < Frames; ++frame)
            if (!video::RenderRgb(board, raw, colors) || !screen_scale::Apply(displayMode, raw, expected))
                return false;
        redrawTimes[i] = std::chrono::duration<double, std::milli>(Clock::now() - start).count() / Frames;

        start = Clock::now();
        for (int frame = 0; frame < Frames; ++frame)
            if (video::UpdateMemorySnapshot(board, snapshot)) return false;
        reuseTimes[i] = std::chrono::duration<double, std::milli>(Clock::now() - start).count() / Frames;
        if (expected != scaled) return false;
    }
    std::ranges::sort(redrawTimes);
    std::ranges::sort(reuseTimes);
    std::printf("Mode %d median ms preparing unchanged video: redraw %.3f, reused %.3f\n",
        displayMode, redrawTimes[Repetitions / 2], reuseTimes[Repetitions / 2]);
    return true;
}

bool BenchmarkUploadLayout(int displayMode)
{
    const auto& size = screen_scale::GetMode(displayMode);
    const auto width = static_cast<std::size_t>(size.width);
    const auto bytesPerRow = width * sizeof(uint32_t);
    std::vector<uint32_t> raw(video::Width * video::Height), scaled(width * size.height);
    std::vector<uint32_t> reversedCopy(scaled.size()), directCopy(scaled.size());
    for (std::size_t i = 0; i < raw.size(); ++i)
        raw[i] = (static_cast<uint32_t>(i) * 3266489917u) & 0xffffffu;

    std::array<std::array<double, Repetitions>, 2> timings{};
    for (int i = 0; i < Repetitions; ++i)
    {
        for (int offset = 0; offset < 2; ++offset)
        {
            const int path = (i + offset) % 2;
            const auto order = path == 0 ? screen_scale::RowOrder::bottomUp : screen_scale::RowOrder::topDown;
            const auto start = Clock::now();
            for (int frame = 0; frame < Frames; ++frame)
            {
                if (!screen_scale::Apply(displayMode, raw, scaled, order)) return false;
                if (order == screen_scale::RowOrder::topDown)
                    std::memcpy(directCopy.data(), scaled.data(), bytesPerRow * size.height);
                else
                    for (int row = 0; row < size.height; ++row)
                        std::memcpy(reversedCopy.data() + row * width,
                            scaled.data() + (size.height - 1 - row) * width, bytesPerRow);
            }
            timings[path][i] = std::chrono::duration<double, std::milli>(Clock::now() - start).count() / Frames;
        }
        if (directCopy != reversedCopy) return false;
    }
    for (auto& times : timings) std::ranges::sort(times);
    std::printf("Mode %d median ms scaling and copying video: reverse rows %.3f, top-down %.3f\n",
        displayMode, timings[0][Repetitions / 2], timings[1][Repetitions / 2]);
    return true;
}
}

int main()
{
    CProcessor::Init();
    const bool success = BenchmarkMode(1) && BenchmarkMode(6) && BenchmarkIdle(1) && BenchmarkIdle(6) &&
        BenchmarkUploadLayout(1) && BenchmarkUploadLayout(6);
    CProcessor::Done();
    return success ? 0 : 1;
}
