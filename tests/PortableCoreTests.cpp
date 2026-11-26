/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "emubase/Emubase.h"
#include "BreakpointSet.h"
#include "FrameControl.h"
#include "FrameWorker.h"
#include "InstructionHistory.h"
#include "Snapshot.h"
#include "VideoDecoder.h"
#include "util/ScreenScale.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <stdexcept>
#include <thread>
#include <vector>

// A frontend supplies the logging sink; this test runs the core headlessly.
void DebugLog(const wchar_t*) {}

namespace {
struct TempImage {
    std::filesystem::path path;
    explicit TempImage(const char* prefix)
    {
        const auto suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        path = std::filesystem::temp_directory_path() / (std::string(prefix) + suffix);
        path += std::filesystem::path(u8"-\u65e5\u672c.img");
    }
    ~TempImage() { std::error_code error; std::filesystem::remove(path, error); }
};

class PortableCore : public testing::Test {
protected:
    void SetUp() override { CProcessor::Init(); }
    void TearDown() override { CProcessor::Done(); }
};

TEST_F(PortableCore, VideoSnapshotMatchesBoardAndSurvivesConcurrentFrame)
{
    CMotherboard board;
    board.Reset();
    board.GetCPU()->SetDCLOPin(true);
    board.GetPPU()->SetDCLOPin(true);
    for (unsigned address = 0; address < 65536; ++address)
        for (int plane = 0; plane < 3; ++plane)
            board.SetRAMByte(plane, static_cast<uint16_t>(address), static_cast<uint8_t>((address * 17 + plane * 31) >> 3));
    video::MemorySnapshot snapshot;
    video::CaptureMemory(board, snapshot);
    std::array<uint32_t, 128> palette{};
    for (unsigned i = 0; i < palette.size(); ++i) palette[i] = i * 0x10101;
    std::vector<uint32_t> expected(video::Width * video::Height), actual(expected.size());
    ASSERT_TRUE(video::RenderRgb(board, expected, palette));
    video::ParallelDecoder videoWorker;
    FrameWorker worker([&] {
        for (unsigned address = 0; address < 65536; ++address)
            board.SetRAMByte(0, static_cast<uint16_t>(address), 0xff);
        return board.SystemFrame();
    });
    ASSERT_TRUE(worker.Start());
    ASSERT_TRUE(videoWorker.Render(snapshot, actual, palette));
    ASSERT_TRUE(worker.Wait());
    EXPECT_EQ(actual, expected);
}

TEST_F(PortableCore, VideoSnapshotPreservesTagsCursorAndAllScales)
{
    CMotherboard board;
    board.Reset();
    board.SetRAMWord(0, 0000270, 0x4000);
    board.SetRAMWord(0, 0000272, 0x1006); // Palette tag.
    board.SetRAMWord(0, 0x1000, 0x3210);
    board.SetRAMWord(0, 0x1002, 0x7654);
    board.SetRAMWord(0, 0x1004, 0x4000);
    board.SetRAMWord(0, 0x1006, 0x1012); // Parameter tag.
    board.SetRAMWord(0, 0x1010, 0x001f); // Graphical cursor.
    board.SetRAMWord(0, 0x1014, 0x4000);
    board.SetRAMWord(0, 0x1016, 0x1019); // Enable cursor and loop over short tags.
    board.SetRAMWord(0, 0x1018, 0x4000);
    board.SetRAMWord(0, 0x101a, 0x2000);
    for (int i = 0; i < video::TotalLines; ++i)
    {
        const auto address = static_cast<uint16_t>(0x2000 + i * 4);
        board.SetRAMWord(0, address, i % 2 ? 0x4000 : 0x4100);
        // Toggle the cursor on the first row decoded by the worker.
        const auto toggle = i == video::HiddenLines + video::Height / 2 - 4 ? 1 : 0;
        board.SetRAMWord(0, address + 2, static_cast<uint16_t>((address + 4) | toggle));
    }
    for (unsigned i = 0; i < 80; ++i)
    {
        board.SetRAMByte(0, static_cast<uint16_t>(0x4000 + i), 0xaa);
        board.SetRAMByte(1, static_cast<uint16_t>(0x4000 + i), 0xcc);
        board.SetRAMByte(2, static_cast<uint16_t>(0x4000 + i), 0xf0);
    }
    std::array<uint32_t, 128> palette{};
    for (unsigned i = 0; i < palette.size(); ++i) palette[i] = i + 1;
    std::vector<uint32_t> expected(video::Width * video::Height), actual(expected.size() + 1, 0xdeadbeef);
    video::MemorySnapshot snapshot;
    video::ParallelDecoder videoWorker;
    for (int shift = 0; shift < 4; ++shift)
    {
        board.SetRAMWord(0, 0x1012, static_cast<uint16_t>((shift << 4) | 7));
        video::CaptureMemory(board, snapshot);
        ASSERT_TRUE(video::RenderRgb(board, expected, palette));
        ASSERT_TRUE(videoWorker.Render(snapshot, actual, palette));
        EXPECT_TRUE(std::ranges::equal(std::span{actual}.first(expected.size()), expected)) << "scale " << shift;
        EXPECT_EQ(actual.back(), 0xdeadbeef);
        EXPECT_EQ(actual.front(), palette[15]);
        EXPECT_EQ(actual[video::Width * (video::Height / 2 - 1)], palette[15]);
        EXPECT_EQ(actual[video::Width * (video::Height / 2)], palette[0]);
    }
    board.SetRAMWord(0, 0xfffe, 0x1234);
    video::CaptureMemory(board, snapshot);
    EXPECT_EQ(snapshot.GetRAMWord(0, 0xffff), board.GetRAMWord(0, 0xffff));
    std::ranges::fill(actual, 0xdeadbeef);
    EXPECT_FALSE(videoWorker.Render(snapshot, std::span{actual}.first(expected.size() - 1), palette));
    EXPECT_FALSE(videoWorker.Render(snapshot, actual, std::span{palette}.first(palette.size() - 1)));
    EXPECT_TRUE(std::ranges::all_of(actual, [](uint32_t value) { return value == 0xdeadbeef; }));
}

TEST_F(PortableCore, VideoSnapshotRefreshDetectsChangesAcrossAllRamPlanes)
{
    CMotherboard board;
    video::MemorySnapshot snapshot;
    EXPECT_FALSE(video::UpdateMemorySnapshot(board, snapshot));
    for (int plane = 0; plane < 3; ++plane)
    {
        board.SetRAMByte(plane, 0, 0xa1);
        board.SetRAMByte(plane, 0xffff, 0xb2);
        EXPECT_TRUE(video::UpdateMemorySnapshot(board, snapshot)) << "RAM plane " << plane;
        EXPECT_EQ(snapshot.GetRAMByte(plane, 0), 0xa1);
        EXPECT_EQ(snapshot.GetRAMByte(plane, 0xffff), 0xb2);
        EXPECT_FALSE(video::UpdateMemorySnapshot(board, snapshot));
    }
    video::MemorySnapshot fresh;
    video::CaptureMemory(board, fresh);
    EXPECT_EQ(snapshot.planes, fresh.planes);
}

TEST_F(PortableCore, ParallelVideoPreparationMatchesSerialInEveryDisplayMode)
{
    CMotherboard board;
    board.Reset();
    board.SetRAMWord(0, 0000270, 0x8000);
    board.SetRAMWord(0, 0000272, 0x7006);
    board.SetRAMWord(0, 0x7000, 0x3210);
    board.SetRAMWord(0, 0x7002, 0x7654);
    board.SetRAMWord(0, 0x7004, 0x8050);
    board.SetRAMWord(0, 0x7006, 0x7008);
    board.SetRAMWord(0, 0x7008, 0x8000);
    board.SetRAMWord(0, 0x700a, 0x7004);
    for (int plane = 0; plane < 3; ++plane)
        for (unsigned i = 0; i < 160; ++i)
            board.SetRAMByte(plane, static_cast<uint16_t>(0x8000 + i), static_cast<uint8_t>(i * (plane + 3)));
    std::array<uint32_t, 128> palette{};
    for (unsigned i = 0; i < palette.size(); ++i) palette[i] = i * 0x010307;
    video::MemorySnapshot snapshot;
    video::CaptureMemory(board, snapshot);
    video::ParallelDecoder parallel;
    video::ParallelDecoder fallback(false);
    const auto owner = std::this_thread::get_id();
    std::vector<uint32_t> expectedRaw(video::Width * video::Height), raw(expectedRaw.size());
    ASSERT_TRUE(video::RenderRgb(snapshot, expectedRaw, palette));
    for (int mode = -1; mode < static_cast<int>(screen_scale::Modes().size()); ++mode)
    {
        const auto& size = screen_scale::GetMode(mode);
        std::vector<uint32_t> expected(static_cast<std::size_t>(size.width) * size.height);
        std::vector<uint32_t> actual(expected.size() + 1, 0xdeadbeef);
        for (const auto order : {screen_scale::RowOrder::bottomUp, screen_scale::RowOrder::topDown})
        {
            ASSERT_TRUE(screen_scale::Apply(mode, expectedRaw, expected, order));
            for (auto& decoder : {std::ref(parallel), std::ref(fallback)})
            {
                int uiCalls = 0;
                const auto prepareUi = [&] {
                    EXPECT_EQ(std::this_thread::get_id(), owner);
                    ++uiCalls;
                };
                std::ranges::fill(raw, 0xdeadbeef);
                std::ranges::fill(actual, 0xdeadbeef);
                EXPECT_FALSE(decoder.get().RenderScaled(snapshot, raw, palette, mode,
                    std::span{actual}.first(expected.size() - 1), prepareUi, order));
                EXPECT_EQ(uiCalls, 0);
                EXPECT_EQ(raw.front(), 0xdeadbeef);
                EXPECT_EQ(actual.front(), 0xdeadbeef);
                ASSERT_TRUE(decoder.get().RenderScaled(snapshot, raw, palette, mode, actual, prepareUi, order));
                EXPECT_EQ(uiCalls, 1);
                EXPECT_EQ(raw, expectedRaw);
                EXPECT_TRUE(std::ranges::equal(std::span{actual}.first(expected.size()), expected)) << "mode " << mode;
                EXPECT_EQ(actual.back(), 0xdeadbeef);
            }
        }
    }
}

TEST_F(PortableCore, VideoDecoderCanRunAgainAfterGuiPreparationThrows)
{
    const video::MemorySnapshot snapshot;
    const std::array<uint32_t, 128> palette{};
    std::vector<uint32_t> raw(video::Width * video::Height), scaled(raw.size());
    video::ParallelDecoder decoder;
    EXPECT_THROW((void)decoder.RenderScaled(snapshot, raw, palette, 0, scaled,
        [] { throw std::runtime_error("GUI preparation"); }), std::runtime_error);
    EXPECT_TRUE(decoder.RenderScaled(snapshot, raw, palette, 0, scaled));
}

TEST_F(PortableCore, VideoDecoderCollectsItsBandWhenTheHostWaitReturnsOrThrows)
{
    const video::MemorySnapshot snapshot;
    const std::array<uint32_t, 128> palette{};
    std::vector<uint32_t> raw(video::Width * video::Height), scaled(raw.size());
    std::vector<uint32_t> expected(raw.size());
    ASSERT_TRUE(video::RenderRgb(snapshot, expected, palette));
    const auto owner = std::this_thread::get_id();
    int waits = 0;
    video::ParallelDecoder decoder(true, {
        .waitUntilReady = [&](FrameWorker&) {
            EXPECT_EQ(std::this_thread::get_id(), owner);
            if (++waits == 1) throw std::runtime_error("Host wait");
        },
    });
    constexpr auto order = screen_scale::RowOrder::topDown;
    EXPECT_THROW((void)decoder.RenderScaled(snapshot, raw, palette, 0, scaled, {}, order), std::runtime_error);
    std::ranges::fill(raw, 0xdeadbeef);
    std::ranges::fill(scaled, 0xdeadbeef);
    ASSERT_TRUE(decoder.RenderScaled(snapshot, raw, palette, 0, scaled, {}, order));
    EXPECT_EQ(waits, 2);
    EXPECT_EQ(raw, expected);
    EXPECT_EQ(scaled, expected);
}

TEST_F(PortableCore, BoardDecoderAndSnapshotRoundTrip)
{
    TempImage file("ukncbtl-state-");
    CMotherboard board;
    board.Reset();
    board.GetCPU()->SetReg(0, 012345);
    board.SetRAMWord(1, 02000, 065432);

    const std::array<uint16_t, 3> instruction{0012761, 1, 2};
    const auto decoded = DisassembleInstruction(instruction, 01000);
    ASSERT_TRUE(decoded);
    EXPECT_EQ(decoded->mnemonic, L"MOV");
    EXPECT_EQ(decoded->argument, L"#000001, 000002(R1)");

    std::array<uint32_t, 128> palette{};
    std::vector<uint32_t> pixels(video::Width * video::Height);
    EXPECT_TRUE(video::RenderRgb(board, pixels, palette));
    ASSERT_TRUE(snapshot::Save(file.path, board, 123));
    board.GetCPU()->SetReg(0, 0);
    board.SetRAMWord(1, 02000, 0);
    InstructionHistory history;
    history.RecordInstruction(true, 01000, {1, 2, 3}, 0);
    uint32_t uptime = 0;
    int stops = 0;
    ASSERT_TRUE(snapshot::Load(file.path, board, uptime, history, [&] { ++stops; }));
    EXPECT_EQ(stops, 1);
    EXPECT_EQ(uptime, 123u);
    EXPECT_EQ(history.Length(true), 0);
    EXPECT_EQ(board.GetCPU()->GetReg(0), 012345);
    EXPECT_EQ(board.GetRAMWord(1, 02000), 065432);
}

TEST_F(PortableCore, BoardAttachesImagesUsingFilesystemPaths)
{
    TempImage file("ukncbtl-disk-");
    std::vector<uint8_t> bytes(64 * 512, 0);
    bytes[0] = 8; bytes[1] = 2;
    std::ofstream output(file.path, std::ios::binary);
    ASSERT_TRUE(output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    output.close();
    ASSERT_TRUE(output);

    CMotherboard board;
    board.Reset();
    ASSERT_TRUE(board.AttachHardImage(1, file.path));
    EXPECT_TRUE(board.IsHardImageAttached(1));
    board.DetachHardImage(1);
    ASSERT_TRUE(board.AttachFloppyImage(0, file.path));
    EXPECT_TRUE(board.IsFloppyImageAttached(0));
    board.DetachFloppyImage(0);
}

TEST_F(PortableCore, FrameStopsAtBreakpointAndPreservesPermanentTargets)
{
    BreakpointSet cpu, ppu;
    CMotherboard board;
    board.Reset();
    board.GetCPU()->SetDCLOPin(true);
    board.GetPPU()->SetDCLOPin(true);
    const auto start = [](CProcessor* processor) {
        processor->SetACLOPin(true);
        processor->SetDCLOPin(false);
        processor->SetACLOPin(false);
        processor->InterruptProcessing();
        processor->SetPSW(0200);
        processor->SetPC(01000);
        processor->SetSP(0400);
        processor->ClearInternalTick();
        processor->GetMemoryController()->SetWord(01000, false, 0005200); // INC R0
    };
    start(board.GetCPU());
    start(board.GetPPU());
    ASSERT_TRUE(cpu.Add(01002));
    cpu.SetTemporary(04000);
    ppu.SetTemporary(05000);
    frame_control::PrepareRun(board, cpu, ppu);

    EXPECT_FALSE(frame_control::RunFrame(board, cpu, ppu));
    EXPECT_EQ(board.GetCPU()->GetReg(0), 1);
    EXPECT_EQ(board.GetPPU()->GetReg(0), 0);
    EXPECT_TRUE(cpu.Contains(01002));
    EXPECT_FALSE(cpu.Contains(04000));
    EXPECT_FALSE(ppu.Contains(05000));
}

TEST_F(PortableCore, FrameWithoutBreakpointsCompletes)
{
    BreakpointSet cpu, ppu;
    CMotherboard board;
    board.Reset();
    frame_control::PrepareRun(board, cpu, ppu);
    EXPECT_TRUE(frame_control::RunFrame(board, cpu, ppu));
}
}
