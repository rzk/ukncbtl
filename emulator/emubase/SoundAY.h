/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once
#include <array>
#include <cstdint>
#include <span>

class CSoundAY
{
protected:
    int index;
    int ready;
    std::array<unsigned, 16> Regs{};
    int32_t lastEnable;
    int32_t PeriodA, PeriodB, PeriodC, PeriodN, PeriodE;
    int32_t CountA, CountB, CountC, CountN, CountE;
    uint32_t VolA, VolB, VolC, VolE;
    uint8_t EnvelopeA, EnvelopeB, EnvelopeC;
    uint8_t OutputA, OutputB, OutputC, OutputN;
    int8_t CountEnv;
    uint8_t Hold, Alternate, Attack, Holding;
    int32_t RNG;
protected:
    static std::array<unsigned int, 32> VolTable;

public:
    CSoundAY();
    void Reset();

public:
    void SetReg(int r, int v);
    void Callback(std::span<uint8_t> stream);

protected:
    static void BuildMixerTable();
    void RenderChunk(std::span<uint8_t> stream);
};
