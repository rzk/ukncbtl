/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "PreviewTape.h"
#include "TapeTestHooks.h"
#include "FrameWorker.h"
#include "emubase/Emubase.h"
#include <gtest/gtest.h>
#include <chrono>
#include <filesystem>
#include <memory>
#include <algorithm>
#include <thread>

void DebugLog(const wchar_t*) {}
namespace
{
unsigned legacySamples = 0;
bool LegacyRead(unsigned samples) { legacySamples += samples; return false; }
void ContextWrite(void* context, int, unsigned samples) { *static_cast<unsigned*>(context) += samples; }

class PreviewTapeTest : public testing::Test
{
protected:
    std::filesystem::path directory, path;
    std::unique_ptr<CMotherboard> board;
    void SetUp() override
    {
        directory = std::filesystem::temp_directory_path() /
            ("ukncbtl-preview-tape-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        ASSERT_TRUE(std::filesystem::create_directory(directory));
        path = directory / std::filesystem::path(u8"\u65e5\u672c.wav");
        CProcessor::Init();
        board = std::make_unique<CMotherboard>(); board->Reset();
        board->GetCPU()->SetDCLOPin(true); board->GetPPU()->SetDCLOPin(true);
    }
    void TearDown() override
    {
        board.reset(); CProcessor::Done();
        std::error_code error;
        std::filesystem::remove(path, error); std::filesystem::remove(directory, error);
    }
};

TEST_F(PreviewTapeTest, RecordsOnFrameWorkerAndStopsAfterHandoff)
{
    preview_tape::Session tape(*board);
    ASSERT_TRUE(tape.Create(path));
    board->GetPPUMemoryController()->SetWord(0177716, false, 0100002);
    ASSERT_TRUE(tape.Play());
    FrameWorker worker([&] { return board->SystemFrame(); });
    for (unsigned frame = 1; frame <= 3; ++frame)
    {
        ASSERT_TRUE(worker.Start());
        ASSERT_TRUE(worker.Wait());
        EXPECT_EQ(tape.Position(), 882u * frame);
        EXPECT_FALSE(tape.TakeError());
    }
    tape.Stop();
    EXPECT_FALSE(tape.Playing());
    ASSERT_TRUE(tape.Close());
    const auto recorded = WavPcmFile::Open(path);
    ASSERT_TRUE(recorded);
    EXPECT_EQ(recorded->Length(), 2646u);
}

TEST_F(PreviewTapeTest, BufferedRecordingPreservesSamplesAcrossBlocksAndRewind)
{
    preview_tape::Session tape(*board);
    ASSERT_TRUE(tape.Create(path));
    auto* memory = board->GetPPUMemoryController();
    FrameWorker worker([&] { return board->SystemFrame(); });
    ASSERT_TRUE(tape.Play());
    for (unsigned frame = 0; frame < 12; ++frame)
    {
        memory->SetWord(0177716, false, frame % 2 == 0 ? 0100002 : 0100000);
        ASSERT_TRUE(worker.Start());
        ASSERT_TRUE(worker.Wait());
        EXPECT_EQ(tape.Position(), (frame + 1) * 882u);
        EXPECT_FALSE(tape.TakeError());
    }
    EXPECT_EQ(tape.Length(), 12u * 882);
    ASSERT_TRUE(worker.Start([&] { return tape.Rewind(); }));
    ASSERT_TRUE(worker.Wait());
    EXPECT_TRUE(tape.Playing());
    EXPECT_EQ(tape.Position(), 0u);
    EXPECT_EQ(tape.Length(), 12u * 882);
    memory->SetWord(0177716, false, 0100000);
    ASSERT_TRUE(worker.Start());
    ASSERT_TRUE(worker.Wait());
    ASSERT_TRUE(worker.Start([&] { tape.Stop(); return true; }));
    ASSERT_TRUE(worker.Wait());
    ASSERT_FALSE(tape.TakeError());
    ASSERT_TRUE(worker.Start([&] { return tape.Close(); }));
    ASSERT_TRUE(worker.Wait());

    auto recorded = WavPcmFile::Open(path);
    ASSERT_TRUE(recorded);
    ASSERT_EQ(recorded->Length(), 12u * 882);
    for (unsigned frame = 0; frame < 12; ++frame)
    {
        const auto expected = frame != 0 && frame % 2 == 0 ? 0xff000000u : 0u;
        for (unsigned sample = 0; sample < 882; ++sample)
            ASSERT_EQ(recorded->ReadOne(), expected) << "frame " << frame << ", sample " << sample;
    }
}

TEST_F(PreviewTapeTest, RecordingWriterFailureStopsTransportAndReleasesStreamOnClose)
{
    struct ResetFault { ~ResetFault() { preview_tape::testing::FailRecordingWrites(false); } } resetFault;
    preview_tape::Session tape(*board);
    ASSERT_TRUE(tape.Create(path));
    preview_tape::testing::FailRecordingWrites(true);
    board->GetPPUMemoryController()->SetWord(0177716, false, 0100002);
    ASSERT_TRUE(tape.Play());
    for (int frame = 0; frame < 5; ++frame) ASSERT_TRUE(board->SystemFrame());
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    bool failed = tape.TakeError();
    while (!failed && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        failed = tape.TakeError();
    }
    ASSERT_TRUE(failed);
    EXPECT_FALSE(tape.TakeError());
    EXPECT_FALSE(tape.Playing());
    EXPECT_FALSE(tape.Play());
    EXPECT_EQ(tape.Position(), 0u);
    EXPECT_EQ(tape.Length(), 0u);
    EXPECT_FALSE(tape.Close());
    EXPECT_FALSE(tape.Inserted());
    auto wav = WavPcmFile::Open(path);
    ASSERT_TRUE(wav);
    EXPECT_EQ(wav->Length(), 0u);
}

TEST_F(PreviewTapeTest, AsyncOpenHandsPlaybackStreamToBoardBetweenFrames)
{
    {
        auto wav = WavPcmFile::Create(path, 44100);
        ASSERT_TRUE(wav);
        for (int i = 0; i < 1764; ++i) ASSERT_TRUE(wav->WriteOne(0xff000000));
        ASSERT_TRUE(wav->Close());
    }
    preview_tape::Session tape(*board);
    preview_tape::AsyncPrepare open;
    FrameWorker worker([&] { return board->SystemFrame(); });
    ASSERT_TRUE(open.Start(path, preview_tape::FileMode::playback));
    EXPECT_EQ(open.Path(), path);
    EXPECT_FALSE(open.Start(path, preview_tape::FileMode::recording));
    ASSERT_TRUE(worker.Start());
    auto result = open.Finish();
    ASSERT_TRUE(worker.Wait());
    ASSERT_TRUE(result);
    EXPECT_FALSE(open.Pending());
    EXPECT_FALSE(tape.Inserted());
    ASSERT_TRUE(std::move(*result).InsertInto(tape));
    EXPECT_FALSE(result->Ready());
    EXPECT_EQ(tape.Path(), path);
    EXPECT_EQ(tape.Position(), 0u);
    ASSERT_TRUE(tape.Play());
    ASSERT_TRUE(worker.Start());
    ASSERT_TRUE(worker.Wait());
    EXPECT_EQ(tape.Position(), 882u);
    EXPECT_FALSE(tape.TakeError());
}

TEST_F(PreviewTapeTest, AsyncOpenFailureAndOccupiedDrivePreserveTapeState)
{
    preview_tape::Session tape(*board);
    preview_tape::AsyncPrepare open;
    ASSERT_TRUE(open.Start(path, preview_tape::FileMode::playback));
    auto failed = open.Finish();
    ASSERT_TRUE(failed);
    EXPECT_FALSE(failed->Ready());
    EXPECT_FALSE(std::move(*failed).InsertInto(tape));
    EXPECT_FALSE(tape.Inserted());

    ASSERT_TRUE(tape.Create(path));
    ASSERT_TRUE(tape.Close());
    ASSERT_TRUE(open.Start(path, preview_tape::FileMode::playback));
    auto result = open.Finish();
    ASSERT_TRUE(result);
    ASSERT_TRUE(result->Ready());
    ASSERT_TRUE(tape.Open(path));
    EXPECT_FALSE(std::move(*result).InsertInto(tape));
    EXPECT_EQ(tape.Path(), path);
    ASSERT_TRUE(tape.Close());
    EXPECT_TRUE(std::move(*result).InsertInto(tape));
}

TEST_F(PreviewTapeTest, AsyncRecordingCreatesValidHeaderAndRecordsAfterHandoff)
{
    using preview_tape::FileMode;
    preview_tape::AsyncPrepare prepare;
    preview_tape::Session tape(*board);
    FrameWorker worker([&] { return board->SystemFrame(); });
    ASSERT_TRUE(prepare.Start(path, FileMode::recording));
    EXPECT_EQ(prepare.Path(), path);
    ASSERT_TRUE(worker.Start());
    ASSERT_TRUE(worker.Wait());
    EXPECT_FALSE(tape.Inserted());
    auto result = prepare.Finish();
    ASSERT_TRUE(result && result->Ready());
    {
        auto emptyWav = WavPcmFile::Open(path);
        ASSERT_TRUE(emptyWav);
        EXPECT_EQ(emptyWav->Length(), 0u);
    }
    ASSERT_TRUE(std::move(*result).InsertInto(tape));
    EXPECT_TRUE(tape.Recording());
    EXPECT_FALSE(tape.Playing());
    board->GetPPUMemoryController()->SetWord(0177716, false, 0100002);
    ASSERT_TRUE(tape.Play());
    ASSERT_TRUE(worker.Start());
    ASSERT_TRUE(worker.Wait());
    ASSERT_TRUE(tape.Close());
    auto wav = WavPcmFile::Open(path);
    ASSERT_TRUE(wav);
    EXPECT_EQ(wav->Length(), 882u);
}

TEST_F(PreviewTapeTest, PendingRecordingCanBeFinalizedWithoutInsertion)
{
    preview_tape::AsyncPrepare prepare;
    ASSERT_TRUE(prepare.Start(directory / "missing" / "file.wav", preview_tape::FileMode::recording));
    auto failed = prepare.Finish();
    ASSERT_TRUE(failed);
    EXPECT_FALSE(failed->Close());
    ASSERT_TRUE(prepare.Start(path, preview_tape::FileMode::recording));
    auto pending = prepare.Finish();
    ASSERT_TRUE(pending);
    EXPECT_TRUE(pending->Close());
    const auto wav = WavPcmFile::Open(path);
    ASSERT_TRUE(wav);
    EXPECT_EQ(wav->Length(), 0u);
}

TEST_F(PreviewTapeTest, ReadAheadKeepsGuestPositionAndDiscardsOldBlocksOnSeek)
{
    constexpr unsigned samplesPerFrame = 882;
    {
        auto wav = WavPcmFile::Create(path, 44100);
        ASSERT_TRUE(wav);
        for (unsigned frame = 0; frame < 14; ++frame)
        {
            std::array<std::uint8_t, samplesPerFrame> block;
            block.fill(static_cast<std::uint8_t>(frame * 16));
            ASSERT_TRUE(wav->WriteSamples(block));
        }
        ASSERT_TRUE(wav->Close());
    }
    preview_tape::Session tape(*board);
    ASSERT_TRUE(tape.Open(path));
    FrameWorker worker([&] { return board->SystemFrame(); });
    ASSERT_TRUE(tape.Play());
    EXPECT_EQ(tape.Position(), 0u);
    ASSERT_TRUE(worker.Start());
    ASSERT_TRUE(worker.Wait());
    EXPECT_EQ(tape.Position(), samplesPerFrame);
    tape.Stop();
    ASSERT_TRUE(tape.Play());
    ASSERT_TRUE(worker.Start());
    ASSERT_TRUE(worker.Wait());
    EXPECT_EQ(tape.Position(), 2 * samplesPerFrame);
    EXPECT_TRUE(std::ranges::all_of(tape.Waveform(), [](auto sample) { return sample == 16; }));
    ASSERT_TRUE(worker.Start([&] { tape.Stop(); return true; }));
    ASSERT_TRUE(worker.Wait());
    ASSERT_TRUE(worker.Start([&] { return tape.Seek(8 * samplesPerFrame); }));
    ASSERT_TRUE(worker.Wait());
    ASSERT_TRUE(worker.Start([&] { return tape.Play(); }));
    ASSERT_TRUE(worker.Wait());
    for (unsigned frame = 8; frame < 14; ++frame)
    {
        ASSERT_TRUE(worker.Start());
        ASSERT_TRUE(worker.Wait());
        EXPECT_EQ(tape.Position(), (frame + 1) * samplesPerFrame);
        EXPECT_TRUE(std::ranges::all_of(tape.Waveform(), [frame](auto sample) { return sample == frame * 16; }));
        EXPECT_FALSE(tape.TakeError());
    }
    EXPECT_FALSE(tape.Playing());
    ASSERT_TRUE(tape.Rewind());
    ASSERT_TRUE(tape.Play());
    ASSERT_TRUE(worker.Start([&] { return tape.Close(); })); // Eject with a read outstanding.
    EXPECT_TRUE(worker.Wait());
}

TEST_F(PreviewTapeTest, ReadAheadDeliversPartialBlockBeforeReportingFailure)
{
    {
        auto wav = WavPcmFile::Create(path, 44100);
        ASSERT_TRUE(wav);
        const std::vector<std::uint8_t> samples(7000, 0xd3);
        ASSERT_TRUE(wav->WriteSamples(samples));
        ASSERT_TRUE(wav->Close());
    }
    struct ResetFault { ~ResetFault() { preview_tape::testing::FailPlaybackAt(-1); } } resetFault;
    preview_tape::Session tape(*board);
    ASSERT_TRUE(tape.Open(path));
    preview_tape::testing::FailPlaybackAt(4300);
    ASSERT_TRUE(tape.Play());
    for (unsigned frame = 1; frame <= 4; ++frame)
    {
        ASSERT_TRUE(board->SystemFrame());
        EXPECT_EQ(tape.Position(), frame * 882u);
        EXPECT_FALSE(tape.TakeError());
    }
    ASSERT_TRUE(board->SystemFrame());
    EXPECT_EQ(tape.Position(), 4300u);
    EXPECT_TRUE(tape.TakeError());
    EXPECT_FALSE(tape.TakeError());
    EXPECT_FALSE(tape.Playing());
    EXPECT_TRUE(std::ranges::all_of(tape.Waveform(), [](auto sample) { return sample == 0xd3; }));
    preview_tape::testing::FailPlaybackAt(-1);
    ASSERT_TRUE(tape.Seek(4300));
    ASSERT_TRUE(tape.Play());
    ASSERT_TRUE(board->SystemFrame());
    EXPECT_EQ(tape.Position(), 5182u);
    EXPECT_FALSE(tape.TakeError());
}

TEST_F(PreviewTapeTest, RecordsFramesRewindsAndReopensForPlayback)
{
    preview_tape::Session tape(*board);
    ASSERT_TRUE(tape.Create(path));
    EXPECT_TRUE(tape.Recording());
    EXPECT_FALSE(tape.Seek(0));
    EXPECT_TRUE(tape.Conflicts(directory / "." / path.filename()));
    EXPECT_FALSE(tape.Open(path));
    board->GetPPUMemoryController()->SetWord(0177716, false, 0100002);
    ASSERT_TRUE(tape.Play());
    ASSERT_TRUE(board->SystemFrame());
    EXPECT_EQ(tape.Position(), 882);
    tape.Stop();
    EXPECT_FALSE(tape.Playing());
    {
        auto stoppedRecording = WavPcmFile::Open(path);
        ASSERT_TRUE(stoppedRecording);
        EXPECT_EQ(stoppedRecording->Length(), 882u);
    }
    ASSERT_TRUE(tape.Play());
    ASSERT_TRUE(board->SystemFrame());
    tape.Stop();
    EXPECT_FALSE(tape.TakeError());
    EXPECT_EQ(tape.Length(), 1764u);
    ASSERT_TRUE(tape.Rewind());
    EXPECT_EQ(tape.Position(), 0);
    ASSERT_TRUE(tape.Close());
    {
        auto recorded = WavPcmFile::Open(path);
        ASSERT_TRUE(recorded);
        const auto sample = recorded->ReadOne();
        ASSERT_TRUE(sample);
        EXPECT_EQ(*sample, 0xff000000u);
    }
    ASSERT_TRUE(tape.Open(path));
    EXPECT_FALSE(tape.Recording());
    EXPECT_EQ(tape.Length(), 1764);
    ASSERT_TRUE(tape.Play());
    ASSERT_TRUE(board->SystemFrame());
    EXPECT_EQ(tape.Position(), 882);
    ASSERT_TRUE(board->SystemFrame());
    EXPECT_FALSE(tape.Playing());
    EXPECT_FALSE(tape.TakeError());
    ASSERT_TRUE(tape.Close());
}

TEST_F(PreviewTapeTest, PlaybackDrivesTapeInputPort)
{
    {
        auto file = WavPcmFile::Create(path, 44100);
        ASSERT_TRUE(file);
        for (int i = 0; i < 1764; ++i) ASSERT_TRUE(file->WriteOne(i < 882 ? 0 : 0xffffffffu));
        ASSERT_TRUE(file->Close());
    }
    preview_tape::Session tape(*board);
    ASSERT_TRUE(tape.Open(path));
    auto* memory = board->GetPPUMemoryController();
    memory->SetWord(0177716, false, 0100004);
    ASSERT_TRUE(tape.Play());
    ASSERT_TRUE(board->SystemFrame());
    EXPECT_EQ(memory->GetWord(0177716, false, false) & 1, 1);
    EXPECT_TRUE(std::ranges::all_of(tape.Waveform(), [](auto sample) { return sample == 0; }));
    EXPECT_FALSE(tape.Seek(0));
    ASSERT_TRUE(board->SystemFrame());
    EXPECT_EQ(memory->GetWord(0177716, false, false) & 1, 0);
    EXPECT_FALSE(tape.Playing());
    EXPECT_TRUE(std::ranges::all_of(tape.Waveform(), [](auto sample) { return sample == 255; }));
}

TEST_F(PreviewTapeTest, SeeksOnlyStoppedPlaybackAndRetainsPositionOnInvalidRequest)
{
    {
        auto file = WavPcmFile::Create(path, 44100);
        ASSERT_TRUE(file);
        for (int i = 0; i < 1764; ++i) ASSERT_TRUE(file->WriteOne(i < 882 ? 0 : 0xffffffffu));
        ASSERT_TRUE(file->Close());
    }
    preview_tape::Session tape(*board);
    EXPECT_FALSE(tape.Seek(0));
    ASSERT_TRUE(tape.Open(path));
    ASSERT_TRUE(tape.Seek(882));
    EXPECT_EQ(tape.Position(), 882u);
    EXPECT_TRUE(std::ranges::all_of(tape.Waveform(), [](auto sample) { return sample == 128; }));
    EXPECT_FALSE(tape.Seek(1765));
    EXPECT_EQ(tape.Position(), 882u);
    ASSERT_TRUE(tape.Play());
    EXPECT_FALSE(tape.Seek(0));
    ASSERT_TRUE(board->SystemFrame());
    EXPECT_FALSE(tape.Playing());
    EXPECT_EQ(tape.Position(), 1764u);
    EXPECT_TRUE(std::ranges::all_of(tape.Waveform(), [](auto sample) { return sample == 255; }));
    ASSERT_TRUE(tape.Seek(0));
    EXPECT_EQ(tape.Position(), 0u);
    EXPECT_TRUE(std::ranges::all_of(tape.Waveform(), [](auto sample) { return sample == 128; }));
}

TEST_F(PreviewTapeTest, InvalidFilesAndEmptyTapeAreHandled)
{
    preview_tape::Session tape(*board);
    EXPECT_FALSE(tape.Open(path));
    EXPECT_FALSE(tape.Play());
    EXPECT_FALSE(tape.Rewind());
    ASSERT_TRUE(tape.Create(path));
    ASSERT_TRUE(tape.Close());
    ASSERT_TRUE(tape.Open(path));
    ASSERT_TRUE(tape.Play());
    ASSERT_TRUE(board->SystemFrame());
    EXPECT_FALSE(tape.Playing());
    EXPECT_FALSE(tape.TakeError());
}

TEST_F(PreviewTapeTest, DestructionDetachesCallbackAndFinalizesRecording)
{
    {
        preview_tape::Session tape(*board);
        ASSERT_TRUE(tape.Create(path));
        ASSERT_TRUE(tape.Play());
        ASSERT_TRUE(board->SystemFrame());
    }
    ASSERT_TRUE(board->SystemFrame());
    auto file = WavPcmFile::Open(path);
    ASSERT_TRUE(file);
    EXPECT_EQ(file->Length(), 882);
}

TEST_F(PreviewTapeTest, LegacyAndContextCallbacksReplaceEachOther)
{
    unsigned contextSamples = 0;
    legacySamples = 0;
    board->SetTapeReadCallback(LegacyRead, 44100);
    ASSERT_TRUE(board->SystemFrame());
    EXPECT_EQ(legacySamples, 882);
    board->SetTapeWriteCallbackWithContext(ContextWrite, &contextSamples, 44100);
    ASSERT_TRUE(board->SystemFrame());
    EXPECT_EQ(contextSamples, 882);
    EXPECT_EQ(legacySamples, 882);
    board->SetTapeReadCallback(LegacyRead, 44100);
    ASSERT_TRUE(board->SystemFrame());
    EXPECT_EQ(legacySamples, 1764);
    EXPECT_EQ(contextSamples, 882);
    board->SetTapeReadCallback(nullptr, 0);
}
}
