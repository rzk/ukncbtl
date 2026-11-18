/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once
#include <array>
#include <cstdint>
#include <span>

class CMotherboard;

namespace video {
inline constexpr int Width = 640;
inline constexpr int Height = 288;
inline constexpr int HiddenLines = 19;
inline constexpr int TotalLines = HiddenLines + Height;
enum class TagKind { Short, Parameters, Palette };

// Immutable frame-boundary copy; safe to decode while the board advances.
struct MemorySnapshot
{
    std::array<std::array<uint8_t, 65536>, 3> planes{};
    [[nodiscard]] uint8_t GetRAMByte(int plane, uint16_t address) const { return planes[plane][address]; }
    [[nodiscard]] uint16_t GetRAMWord(int plane, uint16_t address) const
    {
        address &= 0xfffe;
        return static_cast<uint16_t>(planes[plane][address] | (planes[plane][address + 1] << 8));
    }
};
void CaptureMemory(const CMotherboard& board, MemorySnapshot& snapshot);

struct DisplayTag
{
    uint16_t address;
    TagKind kind;
    uint16_t tag1 = 0, tag2 = 0;
    uint16_t pixels = 0, link = 0;

    [[nodiscard]] TagKind NextKind() const
    { return !(link & 2) ? TagKind::Short : (link & 4) ? TagKind::Palette : TagKind::Parameters; }
    [[nodiscard]] uint16_t NextAddress() const
    { return static_cast<uint16_t>(link & ((link & 2) ? ~7 : ~3)); }
    [[nodiscard]] bool ToggleCursor() const { return (link & 1) != 0; }
    [[nodiscard]] int Scale() const { return 1 << ((tag2 >> 4) & 3); }
};

// One tag per scanline; the first tag is always two words at octal 0270.
class DisplayListCursor
{
public:
    DisplayTag Next(const CMotherboard& board);
    DisplayTag Next(const MemorySnapshot& snapshot);
private:
    template<class Memory> DisplayTag NextFrom(const Memory& memory);
    uint16_t address_ = 0000270;
    TagKind kind_ = TagKind::Short;
};

// Return false without modifying output if either span is too small.
bool RenderRgb(const CMotherboard& board, std::span<uint32_t> pixels, std::span<const uint32_t> colors);
bool RenderRgb(const MemorySnapshot& snapshot, std::span<uint32_t> pixels, std::span<const uint32_t> colors);
// Text preparation ignores palette/cursor and does not expand scaled pixels.
// Each row's unused tail and any storage beyond the frame remain untouched.
bool RenderText(const CMotherboard& board, std::span<uint32_t> pixels, std::span<const uint32_t> colors);
}
