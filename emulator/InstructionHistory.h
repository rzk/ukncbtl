/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

inline constexpr int INSTR_HISTORY_SIZE = 256;

struct InstructionRecord
{
    uint16_t pc; // Instruction address, or PC before the interrupt.
    std::array<uint16_t, 3> words; // Opcode and extra words, or vector/new PC/new PSW.
    uint16_t psw;
    bool interrupt;
};

// Each processor retains its most recent records, independently of tracing.
class InstructionHistory
{
public:
    [[nodiscard]] int Length(bool cpu) const { return rings_[cpu].length; }

    // Index zero is newest. Callers must use an index below Length(cpu).
    [[nodiscard]] const InstructionRecord& Get(bool cpu, int index) const
    {
        const auto& ring = rings_[cpu];
        assert(index >= 0 && index < ring.length);
        return ring.records[(ring.head + INSTR_HISTORY_SIZE - 1 - index) % INSTR_HISTORY_SIZE];
    }

    void Clear()
    {
        for (auto& ring : rings_)
        {
            ring.head = 0;
            ring.length = 0;
        }
    }

    void RecordInstruction(bool cpu, uint16_t pc, std::array<uint16_t, 3> words, uint16_t psw)
    { Add(cpu, {pc, words, psw, false}); }

    void RecordInterrupt(bool cpu, uint16_t oldPc, uint16_t vector, uint16_t newPc, uint16_t newPsw)
    { Add(cpu, {oldPc, {vector, newPc, newPsw}, newPsw, true}); }

    [[nodiscard]] static const wchar_t* InterruptName(uint16_t vector)
    {
        // Ignore the SEL register high byte on HALT-mode vectors.
        switch (vector & 0377)
        {
        case 0000: return L"START";
        case 0004: return L"BUS";
        case 0010: return L"RSVD";
        case 0014: return L"BPT/TBIT";
        case 0020: return L"IOT";
        case 0024: return L"ACLO";
        case 0030: return L"EMT";
        case 0034: return L"TRAP";
        case 0100: return L"EVNT";
        case 0170: return L"HALT";
        case 0174: return L"HALT BUS";
        default: return L"VIRQ";
        }
    }

private:
    struct Ring
    {
        std::array<InstructionRecord, INSTR_HISTORY_SIZE> records{};
        int head = 0;
        int length = 0;
    };
    std::array<Ring, 2> rings_{}; // PPU, CPU.

    void Add(bool cpu, const InstructionRecord& record)
    {
        auto& ring = rings_[cpu];
        ring.records[ring.head] = record;
        ring.head = (ring.head + 1) % INSTR_HISTORY_SIZE;
        if (ring.length < INSTR_HISTORY_SIZE) ++ring.length;
    }
};
