/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "Workloads.h"
#include "FrameWorker.h"
#include "PreviewTape.h"
#include "util/WavPcmFile.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>

void DebugLog(const wchar_t*) {}

namespace
{
constexpr int Frames = 200;
constexpr int Repetitions = 5;
using Clock = std::chrono::steady_clock;
enum class Mode { noTape, direct, buffered };

struct DirectRecording
{
    std::optional<WavPcmFile> file;
    std::array<std::uint8_t, 256> waveform{};
    bool failed = false;

    static void Write(void* context, int value, unsigned samples)
    {
        auto& recording = *static_cast<DirectRecording*>(context);
        const auto result = tape::Write(*recording.file, static_cast<std::uint32_t>(value), samples, recording.waveform);
        recording.failed |= result.error.has_value();
    }
};

double Measure(const std::filesystem::path& path, Mode mode)
{
    CMotherboard board;
    workloads::Prepare(board);
    preview_tape::Session tape(board);
    DirectRecording direct;
    if (mode == Mode::direct)
    {
        auto file = WavPcmFile::Create(path, 44100);
        if (!file) return -1;
        direct.file.emplace(std::move(*file));
        board.SetTapeWriteCallbackWithContext(DirectRecording::Write, &direct, 44100);
    }
    if (mode == Mode::buffered && (!tape.Create(path) || !tape.Play())) return -1;
    FrameWorker worker([&] { return board.SystemFrame(); });
    const auto start = Clock::now();
    for (int frame = 0; frame < Frames; ++frame)
        if (!worker.Start() || !worker.Wait()) return -1;
    if (mode == Mode::buffered)
    {
        tape.Stop();
        if (tape.TakeError() || !tape.Close()) return -1;
    }
    if (mode == Mode::direct)
    {
        board.SetTapeWriteCallbackWithContext(nullptr, nullptr, 0);
        if (direct.failed || !direct.file->Sync() || !direct.file->Close()) return -1;
    }
    const auto elapsed = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    if (mode != Mode::noTape)
    {
        auto file = WavPcmFile::Open(path);
        if (!file || file->Frequency() != 44100 || file->Length() != Frames * 44100 / FRAMERATE) return -1;
    }
    return elapsed / Frames;
}

bool SameRecording(const std::filesystem::path& first, const std::filesystem::path& second)
{
    std::ifstream a(first, std::ios::binary), b(second, std::ios::binary);
    if (!a || !b) return false;
    using It = std::istreambuf_iterator<char>;
    return std::equal(It(a), It(), It(b), It());
}

struct DirectPlayback
{
    CMotherboard& board;
    WavPcmFile& file;
    std::array<std::uint8_t, 256> waveform{};
    bool failed = false;

    static bool Read(void* context, unsigned samples)
    {
        auto& playback = *static_cast<DirectPlayback*>(context);
        const auto result = tape::Read(playback.file, samples, playback.waveform);
        playback.failed |= result.error.has_value();
        if (result.stop) playback.board.SetTapeReadCallbackWithContext(nullptr, nullptr, 0);
        return result.bit;
    }
};

double MeasurePlayback(const std::filesystem::path& path, bool buffered)
{
    CMotherboard board;
    workloads::Prepare(board);
    auto* ports = board.GetPPUMemoryController();
    ports->SetWord(0177716, false, 0100004);
    preview_tape::Session tape(board);
    auto file = WavPcmFile::Open(path);
    if (!file) return -1;
    DirectPlayback direct{board, *file};
    if (buffered)
    {
        if (!tape.Open(path) || !tape.Play()) return -1;
    }
    else board.SetTapeReadCallbackWithContext(DirectPlayback::Read, &direct, file->Frequency());

    FrameWorker worker([&] { return board.SystemFrame(); });
    const auto start = Clock::now();
    for (int frame = 0; frame < Frames; ++frame)
    {
        if (!worker.Start() || !worker.Wait()) return -1;
        const auto waveform = buffered ? tape.Waveform() : std::span<const std::uint8_t>{direct.waveform};
        const auto expected = frame % 2 == 0 ? 0 : 255;
        if (waveform.back() != expected || (ports->GetWord(0177716, false, false) & 1) != (expected == 0)) return -1;
    }
    if (buffered)
    {
        if (tape.Playing() || tape.Position() != tape.Length() || tape.TakeError() || !tape.Close()) return -1;
    }
    else if (direct.failed || file->Position() != file->Length() || !file->Close()) return -1;
    const auto elapsed = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    return elapsed / Frames;
}

bool MakePlaybackFixture(const std::filesystem::path& path)
{
    auto file = WavPcmFile::Create(path, 44100);
    if (!file) return false;
    std::array<std::uint8_t, 44100 / FRAMERATE> samples;
    for (int frame = 0; frame < Frames; ++frame)
    {
        samples.fill(frame % 2 == 0 ? 0 : 255);
        if (!file->WriteSamples(samples)) return false;
    }
    return file->Close().has_value();
}
}

int main()
{
    const auto directory = std::filesystem::temp_directory_path() /
        ("ukncbtl-tape-benchmark-" + std::to_string(Clock::now().time_since_epoch().count()));
    std::error_code error;
    if (!std::filesystem::create_directory(directory, error)) return 1;
    const auto directPath = directory / "direct.wav";
    const auto bufferedPath = directory / "buffered.wav";
    const auto playbackPath = directory / "playback.wav";
    struct Cleanup
    {
        std::filesystem::path directory, direct, buffered, playback;
        ~Cleanup()
        {
            std::error_code ec;
            std::filesystem::remove(direct, ec);
            std::filesystem::remove(buffered, ec);
            std::filesystem::remove(playback, ec);
            std::filesystem::remove(directory, ec);
        }
    } cleanup{directory, directPath, bufferedPath, playbackPath};
    if (!MakePlaybackFixture(playbackPath)) return 1;
    CProcessor::Init();
    std::array<double, Repetitions> baseline{}, direct{}, buffered{};
    std::array<double, Repetitions> directPlayback{}, bufferedPlayback{};
    bool success = true;
    for (int i = 0; i < Repetitions; ++i)
    {
        baseline[i] = Measure({}, Mode::noTape);
        if (i % 2 == 0)
        {
            direct[i] = Measure(directPath, Mode::direct);
            buffered[i] = Measure(bufferedPath, Mode::buffered);
            directPlayback[i] = MeasurePlayback(playbackPath, false);
            bufferedPlayback[i] = MeasurePlayback(playbackPath, true);
        }
        else
        {
            buffered[i] = Measure(bufferedPath, Mode::buffered);
            direct[i] = Measure(directPath, Mode::direct);
            bufferedPlayback[i] = MeasurePlayback(playbackPath, true);
            directPlayback[i] = MeasurePlayback(playbackPath, false);
        }
        if (baseline[i] < 0 || direct[i] < 0 || buffered[i] < 0 || directPlayback[i] < 0 ||
            bufferedPlayback[i] < 0 || !SameRecording(directPath, bufferedPath))
        { success = false; break; }
    }
    CProcessor::Done();
    if (!success) { std::fprintf(stderr, "Tape workload failed or produced different WAV output.\n"); return 1; }
    std::ranges::sort(baseline);
    std::ranges::sort(direct);
    std::ranges::sort(buffered);
    std::ranges::sort(directPlayback);
    std::ranges::sort(bufferedPlayback);
    std::printf("Median milliseconds per synthetic frame: no tape %.3f, direct recording %.3f, buffered recording %.3f\n",
        baseline[Repetitions / 2], direct[Repetitions / 2], buffered[Repetitions / 2]);
    std::printf("Median milliseconds per synthetic frame: direct playback %.3f, read-ahead playback %.3f\n",
        directPlayback[Repetitions / 2], bufferedPlayback[Repetitions / 2]);
}
