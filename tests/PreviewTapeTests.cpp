/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "PreviewTape.h"
#include "emubase/Emubase.h"
#include <gtest/gtest.h>
#include <chrono>
#include <filesystem>
#include <memory>
#include <algorithm>

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
    EXPECT_EQ(tape.Length(), 882);
    ASSERT_TRUE(tape.Play());
    ASSERT_TRUE(board->SystemFrame());
    EXPECT_EQ(tape.Position(), 882);
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
