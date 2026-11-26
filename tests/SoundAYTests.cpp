/*  This file is part of UKNCBTL.
    UKNCBTL is free software: you can redistribute it and/or modify it under the terms
of the GNU Lesser General Public License as published by the Free Software Foundation,
either version 3 of the License, or (at your option) any later version.
    UKNCBTL is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
See the GNU Lesser General Public License for more details.
    You should have received a copy of the GNU Lesser General Public License along with
UKNCBTL. If not, see <http://www.gnu.org/licenses/>. */

// SoundAYTests.cpp : tests for the AY-3-8910 sound chip emulation
//
// The expected behavior is the one of the chip datasheet: the tone frequency is inversely
// proportional to the period, the volume steps are 3 dB, a channel with the tone and noise
// disabled outputs a constant level, the three channels are mixed by addition, and the
// envelope shapes are the ones of the datasheet table. The tests do not rely on the
// sample rate mapping of the emulator, only on ratios and shapes.

#include <gtest/gtest.h>
#include <cwchar>
#include <iterator>
#include <cmath>
#include <vector>
#include "TestHelpers.h"

namespace
{

// Registers of the chip
const int AY_AFINE = 0, AY_ACOARSE = 1, AY_BFINE = 2, AY_BCOARSE = 3, AY_CFINE = 4, AY_CCOARSE = 5;
const int AY_NOISEPER = 6, AY_ENABLE = 7, AY_AVOL = 8, AY_BVOL = 9, AY_CVOL = 10;
const int AY_EFINE = 11, AY_ECOARSE = 12, AY_ESHAPE = 13;

const int ENVELOPE = 0x10;      // volume register bit: use the envelope
const int ALL_OFF = 0x3F;       // enable register: tones and noises are disabled
const int TONE_A_ONLY = 0x3E;   // enable register: only the tone A is on
const int NOISE_A_ONLY = 0x37;  // enable register: only the noise on the channel A
const int LONG_PERIOD = 0xFFF;  // tone period for channels with the tone disabled

std::vector<uint8_t> Render(CSoundAY& ay, int samples)
{
    std::vector<uint8_t> buffer(samples);
    ay.Callback(buffer);
    return buffer;
}

int MaxOf(const std::vector<uint8_t>& samples)
{
    int result = 0;
    for (size_t i = 0; i < samples.size(); i++)
        if (samples[i] > result) result = samples[i];
    return result;
}

// Number of rises through the half of the maximum level
int CountRises(const std::vector<uint8_t>& samples)
{
    int level = MaxOf(samples) / 2;
    int rises = 0;
    bool high = false;
    for (size_t i = 0; i < samples.size(); i++)
    {
        bool nowHigh = samples[i] > level;
        if (nowHigh && !high) rises++;
        high = nowHigh;
    }
    return rises;
}

void SetTonePeriodA(CSoundAY& ay, int period)
{
    ay.SetReg(AY_AFINE, period & 0xFF);
    ay.SetReg(AY_ACOARSE, period >> 8);
}

// Tone and noise disabled on the channel: its output is a constant level. The emulator keeps it so
// only while the tone counter of the channel does not expire, hence the long tone periods.
void SetLongPeriods(CSoundAY& ay)
{
    ay.SetReg(AY_AFINE, LONG_PERIOD & 0xFF);
    ay.SetReg(AY_ACOARSE, LONG_PERIOD >> 8);
    ay.SetReg(AY_BFINE, LONG_PERIOD & 0xFF);
    ay.SetReg(AY_BCOARSE, LONG_PERIOD >> 8);
    ay.SetReg(AY_CFINE, LONG_PERIOD & 0xFF);
    ay.SetReg(AY_CCOARSE, LONG_PERIOD >> 8);
}

// A constant level of the channel A with the given volume register value
int DcLevel(int volume)
{
    CSoundAY ay;
    SetLongPeriods(ay);
    ay.SetReg(AY_ENABLE, ALL_OFF);
    ay.SetReg(AY_AVOL, volume);
    std::vector<uint8_t> samples = Render(ay, 64);
    return samples[32];
}

// The envelope on the channel A, tone and noise are off, the level is the envelope value
std::vector<uint8_t> RenderEnvelope(int shape, int period, int samples)
{
    CSoundAY ay;
    SetLongPeriods(ay);
    ay.SetReg(AY_ENABLE, ALL_OFF);
    ay.SetReg(AY_AVOL, ENVELOPE);
    ay.SetReg(AY_EFINE, period & 0xFF);
    ay.SetReg(AY_ECOARSE, period >> 8);
    ay.SetReg(AY_ESHAPE, shape);
    return Render(ay, samples);
}

bool IsNonIncreasing(const std::vector<uint8_t>& s)
{
    for (size_t i = 1; i < s.size(); i++)
        if (s[i] > s[i - 1]) return false;
    return true;
}

bool IsNonDecreasing(const std::vector<uint8_t>& s)
{
    for (size_t i = 1; i < s.size(); i++)
        if (s[i] < s[i - 1]) return false;
    return true;
}

// Number of the places where the level goes up / down
int CountUps(const std::vector<uint8_t>& s)
{
    int count = 0;
    for (size_t i = 1; i < s.size(); i++)
        if (s[i] > s[i - 1]) count++;
    return count;
}

int CountDowns(const std::vector<uint8_t>& s)
{
    int count = 0;
    for (size_t i = 1; i < s.size(); i++)
        if (s[i] < s[i - 1]) count++;
    return count;
}

}

TEST(SoundAYTests, SoundAY_SilentAfterReset)
{
    CSoundAY ay;
    std::vector<uint8_t> samples = Render(ay, 200);
    ASSERT_EQ(0, MaxOf(samples)) << L"silence";
}

TEST(SoundAYTests, SoundAY_ZeroVolumeIsSilent)
{
    CSoundAY ay;
    ay.SetReg(AY_ENABLE, TONE_A_ONLY);
    SetTonePeriodA(ay, 20);
    ay.SetReg(AY_AVOL, 0);
    ASSERT_EQ(0, MaxOf(Render(ay, 400))) << L"tone with the volume 0";
}

TEST(SoundAYTests, SoundAY_ToneIsASquareWave)
{
    CSoundAY ay;
    ay.SetReg(AY_ENABLE, TONE_A_ONLY);
    SetTonePeriodA(ay, 20);
    ay.SetReg(AY_AVOL, 15);
    std::vector<uint8_t> samples = Render(ay, 400);

    int high = MaxOf(samples);
    ASSERT_TRUE(high > 0) << L"the tone is audible";
    int highCount = 0, lowCount = 0;
    for (size_t i = 0; i < samples.size(); i++)
    {
        if (samples[i] == high) highCount++;
        else if (samples[i] == 0) lowCount++;
    }
    ASSERT_TRUE(highCount > 100) << L"the wave is high for a half of the time";
    ASSERT_TRUE(lowCount > 100) << L"the wave is low for a half of the time";
}

TEST(SoundAYTests, SoundAY_FrequencyIsInverselyProportionalToPeriod)
{
    int rises[3];
    static const int periods[3] = { 20, 40, 80 };
    for (int i = 0; i < 3; i++)
    {
        CSoundAY ay;
        ay.SetReg(AY_ENABLE, TONE_A_ONLY);
        SetTonePeriodA(ay, periods[i]);
        ay.SetReg(AY_AVOL, 15);
        rises[i] = CountRises(Render(ay, 1600));
    }
    ASSERT_TRUE(rises[0] >= 20) << L"enough periods to measure";
    ASSERT_TRUE(abs(rises[0] - 2 * rises[1]) <= 2) << L"period 40 is half the frequency of period 20";
    ASSERT_TRUE(abs(rises[1] - 2 * rises[2]) <= 2) << L"period 80 is half the frequency of period 40";
}

TEST(SoundAYTests, SoundAY_CoarseTonePeriodRegisterCounts256TimesMore)
{
    // The tone period is fine + 256 * coarse
    CSoundAY a;
    a.SetReg(AY_ENABLE, TONE_A_ONLY);
    a.SetReg(AY_AFINE, 0);
    a.SetReg(AY_ACOARSE, 1);  // period 256
    a.SetReg(AY_AVOL, 15);
    CSoundAY b;
    b.SetReg(AY_ENABLE, TONE_A_ONLY);
    b.SetReg(AY_AFINE, 128);
    b.SetReg(AY_ACOARSE, 0);  // period 128
    b.SetReg(AY_AVOL, 15);

    int ra = CountRises(Render(a, 8000));
    int rb = CountRises(Render(b, 8000));
    ASSERT_TRUE(ra >= 10) << L"enough periods to measure";
    ASSERT_TRUE(abs(rb - 2 * ra) <= 2) << L"period 256 is half the frequency of period 128";
}

TEST(SoundAYTests, SoundAY_PeriodZeroIsPeriodOne)
{
    // Datasheet: the period 0 is the same as the period 1
    CSoundAY a;
    a.SetReg(AY_ENABLE, TONE_A_ONLY);
    a.SetReg(AY_AVOL, 15);
    SetTonePeriodA(a, 0);
    CSoundAY b;
    b.SetReg(AY_ENABLE, TONE_A_ONLY);
    b.SetReg(AY_AVOL, 15);
    SetTonePeriodA(b, 1);

    std::vector<uint8_t> sa = Render(a, 300);
    std::vector<uint8_t> sb = Render(b, 300);
    ASSERT_TRUE(sa == sb) << L"the same waveform";
}

TEST(SoundAYTests, SoundAY_ChannelsAreIndependent)
{
    // The tone B has the double period of the tone A; each channel keeps its own period
    CSoundAY ay;
    ay.SetReg(AY_ENABLE, 0x3C);  // tones A and B on
    SetTonePeriodA(ay, 20);
    ay.SetReg(AY_BFINE, 40);
    ay.SetReg(AY_BCOARSE, 0);
    ay.SetReg(AY_AVOL, 15);
    ay.SetReg(AY_BVOL, 0);  // only A is heard
    int ra = CountRises(Render(ay, 1600));

    CSoundAY ay2;
    ay2.SetReg(AY_ENABLE, 0x3C);
    SetTonePeriodA(ay2, 20);
    ay2.SetReg(AY_BFINE, 40);
    ay2.SetReg(AY_BCOARSE, 0);
    ay2.SetReg(AY_AVOL, 0);
    ay2.SetReg(AY_BVOL, 15);  // only B is heard
    int rb = CountRises(Render(ay2, 1600));

    ASSERT_TRUE(ra >= 20) << L"enough periods to measure";
    ASSERT_TRUE(abs(ra - 2 * rb) <= 2) << L"B is twice slower than A";
}

TEST(SoundAYTests, SoundAY_VolumeSteps)
{
    // A step of 2 in the volume register is 3 dB, the amplitude ratio 1.41; 6 dB is 2; 12 dB is 4
    int v15 = DcLevel(15), v13 = DcLevel(13), v11 = DcLevel(11), v9 = DcLevel(9);
    ASSERT_TRUE(v15 > v13 && v13 > v11 && v11 > v9 && v9 > 0) << L"the volume grows with the register";
    ASSERT_TRUE(fabs((double) v15 / v13 - 1.995) < 0.12) << L"15 vs 13: 6 dB";
    ASSERT_TRUE(fabs((double) v15 / v11 - 3.98) < 0.3) << L"15 vs 11: 12 dB";
    ASSERT_TRUE(fabs((double) v13 / v9 - 3.98) < 0.5) << L"13 vs 9: 12 dB";
}

TEST(SoundAYTests, SoundAY_DisabledToneAndNoiseGiveConstantLevel)
{
    // Datasheet: with the tone and noise disabled the channel outputs a constant level
    CSoundAY ay;
    SetLongPeriods(ay);
    ay.SetReg(AY_ENABLE, ALL_OFF);
    ay.SetReg(AY_AVOL, 15);
    std::vector<uint8_t> samples = Render(ay, 300);
    ASSERT_TRUE(samples[0] > 0) << L"not silent";
    for (size_t i = 1; i < samples.size(); i++)
        ASSERT_EQ((int) samples[0], (int) samples[i]) << L"constant";
}

// The board renders two samples at each tick and uses the second one.
TEST(SoundAYTests, SoundAY_DisabledToneAndNoiseAtShortPeriods)
{
    CSoundAY ay;
    ay.SetReg(AY_ENABLE, ALL_OFF);
    ay.SetReg(AY_AVOL, 15);
    int expected = DcLevel(15);  // the level with a long period
    for (int i = 0; i < 100; i++)
    {
        uint8_t buffer[2];
        ay.Callback(buffer);
        ASSERT_EQ(expected, buffer[1]) << L"constant level";
    }
}

TEST(SoundAYTests, SoundAY_ChannelsAreMixedByAddition)
{
    int one, two, three;
    {
        CSoundAY ay;
        SetLongPeriods(ay);
        ay.SetReg(AY_ENABLE, ALL_OFF);
        ay.SetReg(AY_AVOL, 15);
        one = Render(ay, 64)[32];
    }
    {
        CSoundAY ay;
        SetLongPeriods(ay);
        ay.SetReg(AY_ENABLE, ALL_OFF);
        ay.SetReg(AY_AVOL, 15);
        ay.SetReg(AY_BVOL, 15);
        two = Render(ay, 64)[32];
    }
    {
        CSoundAY ay;
        SetLongPeriods(ay);
        ay.SetReg(AY_ENABLE, ALL_OFF);
        ay.SetReg(AY_AVOL, 15);
        ay.SetReg(AY_BVOL, 15);
        ay.SetReg(AY_CVOL, 15);
        three = Render(ay, 64)[32];
    }
    ASSERT_TRUE(one > 0) << L"one channel";
    ASSERT_TRUE(abs(two - 2 * one) <= 1) << L"two channels are twice louder";
    ASSERT_TRUE(abs(three - 3 * one) <= 2) << L"three channels are three times louder";
}

TEST(SoundAYTests, SoundAY_NoiseIsNotConstantAndDeterministic)
{
    CSoundAY a;
    a.SetReg(AY_ENABLE, NOISE_A_ONLY);
    a.SetReg(AY_NOISEPER, 4);
    a.SetReg(AY_AVOL, 15);
    std::vector<uint8_t> sa = Render(a, 2000);

    int high = MaxOf(sa);
    int rises = CountRises(sa);
    ASSERT_TRUE(high > 0) << L"the noise is audible";
    ASSERT_TRUE(rises > 20) << L"the noise changes";

    CSoundAY b;
    b.SetReg(AY_ENABLE, NOISE_A_ONLY);
    b.SetReg(AY_NOISEPER, 4);
    b.SetReg(AY_AVOL, 15);
    ASSERT_TRUE(sa == Render(b, 2000)) << L"the same register values give the same noise";
}

TEST(SoundAYTests, SoundAY_NoisePeriodSetsTheNoiseSpeed)
{
    CSoundAY fast;
    fast.SetReg(AY_ENABLE, NOISE_A_ONLY);
    fast.SetReg(AY_NOISEPER, 2);
    fast.SetReg(AY_AVOL, 15);
    CSoundAY slow;
    slow.SetReg(AY_ENABLE, NOISE_A_ONLY);
    slow.SetReg(AY_NOISEPER, 16);
    slow.SetReg(AY_AVOL, 15);

    int rf = CountRises(Render(fast, 4000));
    int rs = CountRises(Render(slow, 4000));
    ASSERT_TRUE(rf > 2 * rs) << L"the small period gives much more changes";
}

// Envelope shapes of the datasheet table: C At Al H
// 0 0 x x  \___      0 1 x x  /___
// 1 0 0 0  \\\\      1 0 0 1  \___      1 0 1 0  \/\/      1 0 1 1  \---
// 1 1 0 0  ////      1 1 0 1  /---      1 1 1 0  /\/\      1 1 1 1  /___

TEST(SoundAYTests, SoundAY_EnvelopeDecayAndSilence)
{
    static const int shapes[] = { 0x00, 0x01, 0x02, 0x03, 0x09 };
    for (size_t i = 0; i < sizeof(shapes) / sizeof(shapes[0]); i++)
    {
        std::vector<uint8_t> s = RenderEnvelope(shapes[i], 16, 1000);
        wchar_t msg[40];
        std::swprintf(msg, std::size(msg), L"shape %02o", (unsigned int) shapes[i]);
        ASSERT_TRUE(s[0] >= 0x50) << msg;
        ASSERT_TRUE(IsNonIncreasing(s)) << msg;
        ASSERT_EQ(0, s[999]) << msg;
        ASSERT_TRUE(CountDowns(s) >= 20) << msg;
    }
}

TEST(SoundAYTests, SoundAY_EnvelopeAttackAndSilence)
{
    static const int shapes[] = { 0x04, 0x05, 0x06, 0x07, 0x0F };
    for (size_t i = 0; i < sizeof(shapes) / sizeof(shapes[0]); i++)
    {
        std::vector<uint8_t> s = RenderEnvelope(shapes[i], 16, 1000);
        wchar_t msg[40];
        std::swprintf(msg, std::size(msg), L"shape %02o", (unsigned int) shapes[i]);
        ASSERT_TRUE(s[0] <= 0x10) << msg;
        // rises to the maximum, then drops to silence in one step
        ASSERT_TRUE(MaxOf(s) >= 0x50) << msg;
        ASSERT_EQ(0, s[999]) << msg;
        ASSERT_TRUE(CountUps(s) >= 20) << msg;
        ASSERT_TRUE(CountDowns(s) <= 2) << msg;
    }
}

TEST(SoundAYTests, SoundAY_EnvelopeAttackAndHold)
{
    std::vector<uint8_t> s = RenderEnvelope(0x0D, 16, 1000);  // /---
    ASSERT_TRUE(s[0] <= 0x10) << L"starts low";
    ASSERT_TRUE(IsNonDecreasing(s)) << L"rises";
    ASSERT_TRUE(s[999] >= 0x50) << L"holds the maximum";
    ASSERT_EQ(s[900], s[999]) << L"holds";
}

TEST(SoundAYTests, SoundAY_EnvelopeDecayAndHoldHigh)
{
    std::vector<uint8_t> s = RenderEnvelope(0x0B, 16, 1000);  // \---
    ASSERT_TRUE(s[0] >= 0x50) << L"starts high";
    ASSERT_TRUE(CountDowns(s) >= 20) << L"decays";
    ASSERT_TRUE(s[999] >= 0x50) << L"jumps to the maximum and holds it";
    ASSERT_EQ(s[900], s[999]) << L"holds";
}

TEST(SoundAYTests, SoundAY_EnvelopeRepeats)
{
    // \\\\ decays again and again, //// rises again and again
    std::vector<uint8_t> saw = RenderEnvelope(0x08, 16, 1000);
    int jumpsUp = 0;
    for (size_t i = 1; i < saw.size(); i++)
        if (saw[i] > saw[i - 1] + 0x20) jumpsUp++;
    ASSERT_TRUE(jumpsUp >= 4) << L"\\\\\\\\: the level jumps up at every cycle";
    ASSERT_TRUE(CountDowns(saw) >= 100) << L"\\\\\\\\: and decays";

    std::vector<uint8_t> ramp = RenderEnvelope(0x0C, 16, 1000);
    int jumpsDown = 0;
    for (size_t i = 1; i < ramp.size(); i++)
        if (ramp[i] + 0x20 < ramp[i - 1]) jumpsDown++;
    ASSERT_TRUE(jumpsDown >= 4) << L"////: the level jumps down at every cycle";
    ASSERT_TRUE(CountUps(ramp) >= 100) << L"////: and rises";
}

TEST(SoundAYTests, SoundAY_EnvelopeTriangle)
{
    static const int shapes[] = { 0x0A, 0x0E };  // \/\/ and /\/\ ;
    for (size_t i = 0; i < sizeof(shapes) / sizeof(shapes[0]); i++)
    {
        std::vector<uint8_t> s = RenderEnvelope(shapes[i], 16, 1000);
        wchar_t msg[40];
        std::swprintf(msg, std::size(msg), L"shape %02o", (unsigned int) shapes[i]);
        ASSERT_TRUE(CountUps(s) >= 50) << msg;
        ASSERT_TRUE(CountDowns(s) >= 50) << msg;
        // no big jumps: the level changes smoothly
        for (size_t k = 1; k < s.size(); k++)
            ASSERT_TRUE(abs((int) s[k] - (int) s[k - 1]) < 0x20) << msg;
    }
}

TEST(SoundAYTests, SoundAY_EnvelopePeriodSetsTheSpeed)
{
    // The envelope with the double period decays twice slower
    std::vector<uint8_t> fast = RenderEnvelope(0x00, 16, 1000);
    std::vector<uint8_t> slow = RenderEnvelope(0x00, 32, 1000);
    size_t fastEnd = 0, slowEnd = 0;
    while (fastEnd < fast.size() && fast[fastEnd] != 0) fastEnd++;
    while (slowEnd < slow.size() && slow[slowEnd] != 0) slowEnd++;
    ASSERT_TRUE(fastEnd > 50 && fastEnd < 999) << L"the fast envelope ends";
    ASSERT_TRUE(slowEnd < 999) << L"the slow envelope ends";
    ASSERT_TRUE(abs((int) slowEnd - 2 * (int) fastEnd) <= 8) << L"twice longer";
}

TEST(SoundAYTests, SoundAY_ResetSilencesTheChip)
{
    CSoundAY ay;
    SetLongPeriods(ay);
    ay.SetReg(AY_ENABLE, ALL_OFF);
    ay.SetReg(AY_AVOL, 15);
    ASSERT_TRUE(MaxOf(Render(ay, 100)) > 0) << L"sound before";
    ay.Reset();
    ASSERT_EQ(0, MaxOf(Render(ay, 100))) << L"silence after the reset";
}
