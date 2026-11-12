/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "emubase/Emubase.h"
#include "InstructionHistory.h"
#include "Snapshot.h"
#include "VideoDecoder.h"
#include <gtest/gtest.h>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
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
}
