/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "Workloads.h"
#include "FrameWorker.h"
#include "PreviewDebugger.h"
#include "util/DebugLogFile.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <string>

void DebugLog(const wchar_t*) {}

namespace
{
constexpr int Frames = 3;
constexpr int Repetitions = 3;
using Clock = std::chrono::steady_clock;

struct SynchronousTrace
{
    CMotherboard& board;
    DebugLogFile log;
    bool failed = false;
    // Keep decoding and formatting aligned with Session::OnInstruction; the
    // intended difference is flushing each record instead of batching writes.
    static void Record(void* context, const CProcessor* processor, std::uint16_t address, std::uint16_t opcode)
    {
        auto& self = *static_cast<SynchronousTrace*>(context);
        const auto* memory = processor->GetMemoryControllerConst();
        const bool halt = processor->IsHaltMode();
        const std::array<std::uint16_t, 3> words{opcode,
            memory->GetWordView(static_cast<std::uint16_t>(address + 2), halt, true).value,
            memory->GetWordView(static_cast<std::uint16_t>(address + 4), halt, true).value};
        if (processor != self.board.GetCPU() || self.failed) return;
        const auto decoded = DisassembleInstruction(words, address);
        if (!decoded) return;
        const auto line = std::format("CPU\t{:06o}\t{}\t{}\n", address,
            preview_debugger::ToUtf8(decoded->mnemonic).value_or("?"),
            preview_debugger::ToUtf8(decoded->argument).value_or("?"));
        if (!self.log.Append(line)) self.failed = true;
    }
};

double Measure(const std::filesystem::path& path, bool batched)
{
    CMotherboard board;
    workloads::Prepare(board);
    if (batched)
    {
        preview_debugger::Session debugger(board);
        if (!debugger.StartTrace(path, preview_debugger::TraceInstructionsCpu)) return -1;
        FrameWorker worker([&] { return debugger.RunFrame(); });
        const auto start = Clock::now();
        for (int i = 0; i < Frames; ++i)
            if (!worker.Start() || !worker.Wait()) return -1;
        debugger.StopTrace();
        if (debugger.TakeTraceError()) return -1;
        return std::chrono::duration<double, std::milli>(Clock::now() - start).count() / Frames;
    }
    SynchronousTrace trace{board, DebugLogFile(path)};
    if (!trace.log.Open()) return -1;
    board.SetInstructionCallbackWithContext(SynchronousTrace::Record, &trace);
    FrameWorker worker([&] { return board.SystemFrame(); });
    const auto start = Clock::now();
    for (int i = 0; i < Frames; ++i)
        if (!worker.Start() || !worker.Wait()) return -1;
    board.SetInstructionCallbackWithContext(nullptr, nullptr);
    trace.log.Close();
    if (trace.failed) return -1;
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count() / Frames;
}

std::string Read(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
}

int main()
{
    const auto directory = std::filesystem::temp_directory_path() /
        ("ukncbtl-trace-benchmark-" + std::to_string(Clock::now().time_since_epoch().count()));
    std::error_code error;
    if (!std::filesystem::create_directory(directory, error)) return 1;
    const auto synchronous = directory / "synchronous.log";
    const auto batched = directory / "batched.log";
    struct Cleanup
    {
        std::filesystem::path directory, synchronous, batched;
        ~Cleanup() { std::error_code ec; std::filesystem::remove(synchronous, ec); std::filesystem::remove(batched, ec); std::filesystem::remove(directory, ec); }
    } cleanup{directory, synchronous, batched};
    CProcessor::Init();
    std::array<double, Repetitions> serialTimes{}, batchedTimes{};
    bool success = true;
    for (int i = 0; i < Repetitions; ++i)
    {
        if (i % 2 == 0) { serialTimes[i] = Measure(synchronous, false); batchedTimes[i] = Measure(batched, true); }
        else { batchedTimes[i] = Measure(batched, true); serialTimes[i] = Measure(synchronous, false); }
        const auto expected = Read(synchronous);
        if (serialTimes[i] < 0 || batchedTimes[i] < 0 || expected.empty() || expected != Read(batched))
        { success = false; break; }
    }
    CProcessor::Done();
    if (!success) { std::fprintf(stderr, "Trace workload failed or output differed.\n"); return 1; }
    std::ranges::sort(serialTimes);
    std::ranges::sort(batchedTimes);
    std::printf("Median milliseconds per synthetic traced frame: per-record %.3f, batched %.3f\n",
        serialTimes[Repetitions / 2], batchedTimes[Repetitions / 2]);
}
