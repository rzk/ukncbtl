/*  This file is part of UKNCBTL.
    UKNCBTL is free software: you can redistribute it and/or modify it under the terms
of the GNU Lesser General Public License as published by the Free Software Foundation,
either version 3 of the License, or (at your option) any later version.
    UKNCBTL is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
See the GNU Lesser General Public License for more details.
    You should have received a copy of the GNU Lesser General Public License along with
UKNCBTL. If not, see <http://www.gnu.org/licenses/>. */

// DisasmTests.cpp : tests for the KM1801VM2 disassembler
//
// Expected values come from the PDP-11 / KM1801VM2 instruction encoding, not from the
// emulator output. Cases marked "characterization" only pin the current behavior.

#include <gtest/gtest.h>
#include <cwchar>
#include <iterator>
#include "emubase/Emubase.h"

namespace
{

struct DisasmCase
{
    uint16_t addr;          // address of the instruction
    uint16_t words[3];      // instruction words, zero padded
    const wchar_t* instr;     // expected mnemonic
    const wchar_t* arg;       // expected arguments
    unsigned int length;    // expected length in words
};

void CheckCases(const DisasmCase* cases, size_t count)
{
    for (size_t i = 0; i < count; i++)
    {
        const DisasmCase& c = cases[i];

        wchar_t msg[80];
        std::swprintf(msg, std::size(msg), L"opcode %06o at %06o", (unsigned int) c.words[0], (unsigned int) c.addr);

        const auto decoded = DisassembleInstruction(c.words, c.addr);
        ASSERT_TRUE(decoded.has_value()) << msg;
        ASSERT_STREQ(c.instr, decoded->mnemonic.c_str()) << msg;
        ASSERT_STREQ(c.arg, decoded->argument.c_str()) << msg;
        ASSERT_EQ(c.length, static_cast<unsigned int>(decoded->length)) << msg;
    }
}

struct JumpCase
{
    uint16_t words[2];
    int delta;
};

void CheckJumpCases(const JumpCase* cases, size_t count)
{
    for (size_t i = 0; i < count; i++)
    {
        wchar_t msg[40];
        std::swprintf(msg, std::size(msg), L"opcode %06o", (unsigned int) cases[i].words[0]);
        const auto delta = Disasm_CheckForJump(cases[i].words, 01000);  // address does not matter here
        ASSERT_TRUE(delta.has_value()) << msg;
        ASSERT_EQ(cases[i].delta, *delta) << msg;
    }
}

struct AbsoluteJumpCase
{
    uint16_t addr;          // address of the instruction
    uint16_t words[2];
    int delta;              // the distance in words from the instruction to the target
};

void CheckAbsoluteJumpCases(const AbsoluteJumpCase* cases, size_t count)
{
    for (size_t i = 0; i < count; i++)
    {
        wchar_t msg[60];
        std::swprintf(msg, std::size(msg), L"opcode %06o at %06o", (unsigned int) cases[i].words[0], (unsigned int) cases[i].addr);
        const auto delta = Disasm_CheckForJump(cases[i].words, cases[i].addr);
        ASSERT_TRUE(delta.has_value()) << msg;
        ASSERT_EQ(cases[i].delta, *delta) << msg;
    }
}

}

TEST(DisasmTests, Disasm_NoOperandInstructions)
{
    static const DisasmCase cases[] =
    {
        { 01000, { 0000000 }, L"HALT",  L"", 1 },
        { 01000, { 0000001 }, L"WAIT",  L"", 1 },
        { 01000, { 0000002 }, L"RTI",   L"", 1 },
        { 01000, { 0000003 }, L"BPT",   L"", 1 },
        { 01000, { 0000004 }, L"IOT",   L"", 1 },
        { 01000, { 0000005 }, L"RESET", L"", 1 },
        { 01000, { 0000006 }, L"RTT",   L"", 1 },
        { 01000, { 0000240 }, L"NOP",   L"", 1 },
        { 01000, { 0000241 }, L"CLC",   L"", 1 },
        { 01000, { 0000242 }, L"CLV",   L"", 1 },
        { 01000, { 0000243 }, L"CLVC",  L"", 1 },
        { 01000, { 0000244 }, L"CLZ",   L"", 1 },
        { 01000, { 0000250 }, L"CLN",   L"", 1 },
        { 01000, { 0000257 }, L"CCC",   L"", 1 },
        { 01000, { 0000261 }, L"SEC",   L"", 1 },
        { 01000, { 0000262 }, L"SEV",   L"", 1 },
        { 01000, { 0000264 }, L"SEZ",   L"", 1 },
        { 01000, { 0000270 }, L"SEN",   L"", 1 },
        { 01000, { 0000277 }, L"SCC",   L"", 1 },
    };
    CheckCases(cases, sizeof(cases) / sizeof(cases[0]));
}

TEST(DisasmTests, Disasm_AllConditionCodeInstructions)
{
    // CLx / SEx: bit 4 = set/clear, bits 3..0 = N, Z, V, C; the mnemonic lists the flags as NZVC
    for (int set = 0; set < 2; set++)
    {
        for (int flags = 1; flags < 16; flags++)
        {
            uint16_t memory[2] = { (uint16_t) (0000240 | (set ? 020 : 0) | flags), 0 };

            std::wstring expected;
            expected = set ? L"SE" : L"CL";
            if (flags & 010) expected += L"N";
            if (flags & 004) expected += L"Z";
            if (flags & 002) expected += L"V";
            if (flags & 001) expected += L"C";
            if (flags == 017) expected = set ? L"SCC" : L"CCC";  // all four flags

            wchar_t msg[40];
            std::swprintf(msg, std::size(msg), L"opcode %06o", (unsigned int) memory[0]);
            const auto decoded = DisassembleInstruction(memory, 01000);
            ASSERT_TRUE(decoded.has_value()) << msg;
            ASSERT_STREQ(expected.c_str(), decoded->mnemonic.c_str()) << msg;
        }
    }
}

TEST(DisasmTests, Disasm_HaltModeInstructions)
{
    // Special instructions of the KM1801VM2 HALT mode
    static const DisasmCase cases[] =
    {
        { 01000, { 0000012 }, L"START", L"", 1 },
        { 01000, { 0000016 }, L"STEP",  L"", 1 },
        { 01000, { 0000020 }, L"RSEL",  L"", 1 },
        { 01000, { 0000021 }, L"MFUS",  L"", 1 },
        { 01000, { 0000022 }, L"RCPC",  L"", 1 },
        { 01000, { 0000024 }, L"RCPS",  L"", 1 },
        { 01000, { 0000031 }, L"MTUS",  L"", 1 },
        { 01000, { 0000032 }, L"WCPC",  L"", 1 },
        { 01000, { 0000034 }, L"WCPS",  L"", 1 },
    };
    CheckCases(cases, sizeof(cases) / sizeof(cases[0]));
}

TEST(DisasmTests, Disasm_RtsAndFis)
{
    static const DisasmCase cases[] =
    {
        { 01000, { 0000200 }, L"RTS",    L"R0", 1 },
        { 01000, { 0000205 }, L"RTS",    L"R5", 1 },
        { 01000, { 0000206 }, L"RTS",    L"SP", 1 },
        { 01000, { 0000207 }, L"RETURN", L"",   1 },  // RTS PC
        { 01000, { 0075000 }, L"FADD",   L"R0", 1 },
        { 01000, { 0075015 }, L"FSUB",   L"R5", 1 },
        { 01000, { 0075022 }, L"FMUL",   L"R2", 1 },
        { 01000, { 0075033 }, L"FDIV",   L"R3", 1 },
    };
    CheckCases(cases, sizeof(cases) / sizeof(cases[0]));
}

TEST(DisasmTests, Disasm_AddressingModes)
{
    // Every mode of the destination operand, using CLR
    static const DisasmCase cases[] =
    {
        { 01000, { 0005000 },           L"CLR", L"R0",          1 },
        { 01000, { 0005011 },           L"CLR", L"(R1)",        1 },
        { 01000, { 0005022 },           L"CLR", L"(R2)+",       1 },
        { 01000, { 0005033 },           L"CLR", L"@(R3)+",      1 },
        { 01000, { 0005044 },           L"CLR", L"-(R4)",       1 },
        { 01000, { 0005055 },           L"CLR", L"@-(R5)",      1 },
        { 01000, { 0005061, 0001000 },  L"CLR", L"001000(R1)",  2 },
        { 01000, { 0005072, 0001000 },  L"CLR", L"@001000(R2)", 2 },
        { 01000, { 0005026 },           L"CLR", L"(SP)+",       1 },
        { 01000, { 0005046 },           L"CLR", L"-(SP)",       1 },
    };
    CheckCases(cases, sizeof(cases) / sizeof(cases[0]));
}

TEST(DisasmTests, Disasm_AddressingModesOfPC)
{
    static const DisasmCase cases[] =
    {
        { 01000, { 0005007 },          L"CLR", L"PC",       1 },
        { 01000, { 0005017 },          L"CLR", L"(PC)",     1 },
        { 01000, { 0005027, 0000123 }, L"CLR", L"#000123",  2 },  // immediate
        { 01000, { 0005037, 0177564 }, L"CLR", L"@#177564", 2 },  // absolute
        { 01000, { 0005047 },          L"CLR", L"-(PC)",    1 },
        { 01000, { 0005057 },          L"CLR", L"@-(PC)",   1 },
        // relative: PC after fetching the offset word is 001004, plus offset 10
        { 01000, { 0005067, 0000010 }, L"CLR", L"001014",   2 },
        { 01000, { 0005077, 0000010 }, L"CLR", L"@001014",  2 },
        // negative offset
        { 01000, { 0005067, 0177770 }, L"CLR", L"000774",   2 },
    };
    CheckCases(cases, sizeof(cases) / sizeof(cases[0]));
}

TEST(DisasmTests, Disasm_SingleOperandInstructions)
{
    static const DisasmCase cases[] =
    {
        { 01000, { 0000111 }, L"JMP",  L"(R1)", 1 },
        { 01000, { 0000302 }, L"SWAB", L"R2",   1 },
        { 01000, { 0005000 }, L"CLR",  L"R0",   1 },
        { 01000, { 0005100 }, L"COM",  L"R0",   1 },
        { 01000, { 0005200 }, L"INC",  L"R0",   1 },
        { 01000, { 0005300 }, L"DEC",  L"R0",   1 },
        { 01000, { 0005400 }, L"NEG",  L"R0",   1 },
        { 01000, { 0005500 }, L"ADC",  L"R0",   1 },
        { 01000, { 0005600 }, L"SBC",  L"R0",   1 },
        { 01000, { 0005700 }, L"TST",  L"R0",   1 },
        { 01000, { 0006000 }, L"ROR",  L"R0",   1 },
        { 01000, { 0006100 }, L"ROL",  L"R0",   1 },
        { 01000, { 0006200 }, L"ASR",  L"R0",   1 },
        { 01000, { 0006300 }, L"ASL",  L"R0",   1 },
        { 01000, { 0006700 }, L"SXT",  L"R0",   1 },
        { 01000, { 0106400 }, L"MTPS", L"R0",   1 },
        { 01000, { 0106700 }, L"MFPS", L"R0",   1 },
        // byte variants
        { 01000, { 0105000 }, L"CLRB", L"R0",   1 },
        { 01000, { 0105100 }, L"COMB", L"R0",   1 },
        { 01000, { 0105200 }, L"INCB", L"R0",   1 },
        { 01000, { 0105300 }, L"DECB", L"R0",   1 },
        { 01000, { 0105400 }, L"NEGB", L"R0",   1 },
        { 01000, { 0105500 }, L"ADCB", L"R0",   1 },
        { 01000, { 0105600 }, L"SBCB", L"R0",   1 },
        { 01000, { 0105700 }, L"TSTB", L"R0",   1 },
        { 01000, { 0106000 }, L"RORB", L"R0",   1 },
        { 01000, { 0106100 }, L"ROLB", L"R0",   1 },
        { 01000, { 0106200 }, L"ASRB", L"R0",   1 },
        { 01000, { 0106300 }, L"ASLB", L"R0",   1 },
    };
    CheckCases(cases, sizeof(cases) / sizeof(cases[0]));
}

TEST(DisasmTests, Disasm_TwoOperandInstructions)
{
    static const DisasmCase cases[] =
    {
        { 01000, { 0010001 }, L"MOV",  L"R0, R1", 1 },
        { 01000, { 0020001 }, L"CMP",  L"R0, R1", 1 },
        { 01000, { 0030001 }, L"BIT",  L"R0, R1", 1 },
        { 01000, { 0040001 }, L"BIC",  L"R0, R1", 1 },
        { 01000, { 0050001 }, L"BIS",  L"R0, R1", 1 },
        { 01000, { 0060001 }, L"ADD",  L"R0, R1", 1 },
        { 01000, { 0160001 }, L"SUB",  L"R0, R1", 1 },
        { 01000, { 0110001 }, L"MOVB", L"R0, R1", 1 },
        { 01000, { 0120001 }, L"CMPB", L"R0, R1", 1 },
        { 01000, { 0130001 }, L"BITB", L"R0, R1", 1 },
        { 01000, { 0140001 }, L"BICB", L"R0, R1", 1 },
        { 01000, { 0150001 }, L"BISB", L"R0, R1", 1 },
        { 01000, { 0012243 }, L"MOV",  L"(R2)+, -(R3)", 1 },
    };
    CheckCases(cases, sizeof(cases) / sizeof(cases[0]));
}

TEST(DisasmTests, Disasm_TwoOperandInstructionsWithExtraWords)
{
    static const DisasmCase cases[] =
    {
        // source immediate
        { 01000, { 0012700, 0000123 },          L"MOV", L"#000123, R0",             2 },
        // destination absolute
        { 01000, { 0010037, 0177564 },          L"MOV", L"R0, @#177564",            2 },
        // both operands take a word
        { 01000, { 0016162, 0000010, 0000020 }, L"MOV", L"000010(R1), 000020(R2)",  3 },
        { 01000, { 0012761, 0000001, 0000002 }, L"MOV", L"#000001, 000002(R1)",     3 },
        // both operands relative: PC is 001004 for the source and 001006 for the destination
        { 01000, { 0016767, 0000010, 0000020 }, L"MOV", L"001014, 001026",          3 },
        { 01000, { 0066767, 0000010, 0000020 }, L"ADD", L"001014, 001026",          3 },
    };
    CheckCases(cases, sizeof(cases) / sizeof(cases[0]));
}

TEST(DisasmTests, Disasm_PushAndPopAreShownAsMov)
{
    static const DisasmCase cases[] =
    {
        { 01000, { 0010046 }, L"MOV",  L"R0, -(SP)", 1 },  // PUSH is shown as a regular MOV
        { 01000, { 0012600 }, L"MOV",  L"(SP)+, R0", 1 },  // POP is shown as a regular MOV
        { 01000, { 0110046 }, L"MOVB", L"R0, -(SP)", 1 },
        { 01000, { 0112600 }, L"MOVB", L"(SP)+, R0", 1 },
    };
    CheckCases(cases, sizeof(cases) / sizeof(cases[0]));
}

TEST(DisasmTests, Disasm_Branches)
{
    // Offset +1 word from the address 001000: target = 001000 + 2 + 2 = 001004
    static const DisasmCase cases[] =
    {
        { 01000, { 0000401 }, L"BR",   L"001004", 1 },
        { 01000, { 0001001 }, L"BNE",  L"001004", 1 },
        { 01000, { 0001401 }, L"BEQ",  L"001004", 1 },
        { 01000, { 0002001 }, L"BGE",  L"001004", 1 },
        { 01000, { 0002401 }, L"BLT",  L"001004", 1 },
        { 01000, { 0003001 }, L"BGT",  L"001004", 1 },
        { 01000, { 0003401 }, L"BLE",  L"001004", 1 },
        { 01000, { 0100001 }, L"BPL",  L"001004", 1 },
        { 01000, { 0100401 }, L"BMI",  L"001004", 1 },
        { 01000, { 0101001 }, L"BHI",  L"001004", 1 },
        { 01000, { 0101401 }, L"BLOS", L"001004", 1 },
        { 01000, { 0102001 }, L"BVC",  L"001004", 1 },
        { 01000, { 0102401 }, L"BVS",  L"001004", 1 },
        { 01000, { 0103001 }, L"BHIS", L"001004", 1 },  // same opcode as BCC
        { 01000, { 0103401 }, L"BLO",  L"001004", 1 },  // same opcode as BCS
    };
    CheckCases(cases, sizeof(cases) / sizeof(cases[0]));
}

TEST(DisasmTests, Disasm_BranchOffsets)
{
    static const DisasmCase cases[] =
    {
        { 01000, { 0000400 }, L"BR", L"001002", 1 },  // offset 0: next instruction
        { 01000, { 0000777 }, L"BR", L"001000", 1 },  // offset -1: itself
        { 01000, { 0000776 }, L"BR", L"000776", 1 },  // offset -2
        { 01000, { 0000577 }, L"BR", L"001400", 1 },  // offset +127, the maximum
        { 01000, { 0000600 }, L"BR", L"000402", 1 },  // offset -128, the minimum
        { 00100, { 0000403 }, L"BR", L"000110", 1 },  // other base address
    };
    CheckCases(cases, sizeof(cases) / sizeof(cases[0]));
}

TEST(DisasmTests, Disasm_EmtAndTrap)
{
    static const DisasmCase cases[] =
    {
        { 01000, { 0104000 }, L"EMT",  L"000", 1 },
        { 01000, { 0104012 }, L"EMT",  L"012", 1 },
        { 01000, { 0104377 }, L"EMT",  L"377", 1 },
        { 01000, { 0104400 }, L"TRAP", L"000", 1 },
        { 01000, { 0104412 }, L"TRAP", L"012", 1 },
        { 01000, { 0104777 }, L"TRAP", L"377", 1 },
    };
    CheckCases(cases, sizeof(cases) / sizeof(cases[0]));
}

TEST(DisasmTests, Disasm_JsrAndCall)
{
    static const DisasmCase cases[] =
    {
        { 01000, { 0004310 },          L"JSR",  L"R3, (R0)",     1 },
        { 01000, { 0004537, 0100000 }, L"JSR",  L"R5, @#100000", 2 },
        { 01000, { 0004711 },          L"CALL", L"(R1)",         1 },  // JSR PC,(R1)
        { 01000, { 0004737, 0123456 }, L"CALL", L"@#123456",     2 },
        { 01000, { 0004767, 0000010 }, L"CALL", L"001014",       2 },
    };
    CheckCases(cases, sizeof(cases) / sizeof(cases[0]));
}

TEST(DisasmTests, Disasm_ArithmeticExtensions)
{
    // The register is in bits 8..6, the operand in bits 5..0; the text is "operand, register"
    static const DisasmCase cases[] =
    {
        { 01000, { 0070102 },          L"MUL",  L"R2, R1",      1 },
        { 01000, { 0071102 },          L"DIV",  L"R2, R1",      1 },
        { 01000, { 0072102 },          L"ASH",  L"R2, R1",      1 },
        { 01000, { 0073102 },          L"ASHC", L"R2, R1",      1 },
        { 01000, { 0070127, 0000005 }, L"MUL",  L"#000005, R1", 2 },
        // XOR is the other way around: "register, operand"
        { 01000, { 0074102 },          L"XOR",  L"R1, R2",      1 },
    };
    CheckCases(cases, sizeof(cases) / sizeof(cases[0]));
}

TEST(DisasmTests, Disasm_Sob)
{
    // SOB jumps back by nn words: target = address + 2 - 2 * nn
    static const DisasmCase cases[] =
    {
        { 01000, { 0077103 }, L"SOB", L"R1, 000774", 1 },
        { 01000, { 0077501 }, L"SOB", L"R5, 001000", 1 },
        { 01000, { 0077177 }, L"SOB", L"R1, 000604", 1 },  // maximum nn = 077
    };
    CheckCases(cases, sizeof(cases) / sizeof(cases[0]));
}

TEST(DisasmTests, Disasm_SobTargetBelowZeroWrapsTo16Bits)
{
    // The address is 16-bit: 000000 + 2 - 2 * 3 = -4 = 177774
    static const DisasmCase cases[] =
    {
        { 00000, { 0077103 }, L"SOB", L"R1, 177774", 1 },
        { 00002, { 0077177 }, L"SOB", L"R1, 177606", 1 },  // 2 + 2 - 126 = -122
    };
    CheckCases(cases, sizeof(cases) / sizeof(cases[0]));
}

TEST(DisasmTests, Disasm_Mark)
{
    // MARK nn: the 6-bit constant nn, not an operand; the instruction is one word long
    static const DisasmCase cases[] =
    {
        { 01000, { 0006400 }, L"MARK", L"0",  1 },
        { 01000, { 0006405 }, L"MARK", L"5",  1 },
        { 01000, { 0006412 }, L"MARK", L"12", 1 },
        { 01000, { 0006477 }, L"MARK", L"77", 1 },
        // low bits that look like an operand with an extra word: still one word
        { 01000, { 0006427, 0123456 }, L"MARK", L"27", 1 },
        { 01000, { 0006467, 0123456 }, L"MARK", L"67", 1 },
    };
    CheckCases(cases, sizeof(cases) / sizeof(cases[0]));
}

TEST(DisasmTests, Disasm_UnknownInstructions)
{
    // Characterization: the opcodes below have no mnemonic in the disassembler
    static const DisasmCase cases[] =
    {
        { 01000, { 0000007 }, L"unknown", L"000007", 1 },
        { 01000, { 0170000 }, L"unknown", L"170000", 1 },
    };
    CheckCases(cases, sizeof(cases) / sizeof(cases[0]));
}

TEST(DisasmTests, Disasm_CheckForJumpBranches)
{
    // Delta is the distance in words from the instruction to the jump target
    static const JumpCase cases[] =
    {
        { { 0000401 },  2 },    // BR +1
        { { 0000400 },  1 },    // BR 0
        { { 0000777 },  0 },    // BR -1: itself
        { { 0000600 }, -127 },  // BR -128
        { { 0000577 },  128 },  // BR +127
        { { 0001003 },  4 },    // BNE
        { { 0003777 },  0 },    // BLE -1
        { { 0100001 },  2 },    // BPL
        { { 0103401 },  2 },    // BLO
    };
    CheckJumpCases(cases, sizeof(cases) / sizeof(cases[0]));
}

TEST(DisasmTests, Disasm_CheckForJumpSobJmpCall)
{
    static const JumpCase cases[] =
    {
        { { 0077103 },          -2 },  // SOB nn=3
        { { 0077100 },           1 },  // SOB nn=0
        { { 0000167, 0000010 },  6 },  // JMP 001014: (8 + 4) / 2
        { { 0004767, 0177770 }, -2 },  // CALL back by 8 bytes
    };
    CheckJumpCases(cases, sizeof(cases) / sizeof(cases[0]));
}

TEST(DisasmTests, Disasm_CheckForJumpAllBranches)
{
    // Every conditional branch and BR with the offset +3 words: the target is 4 words ahead
    static const uint16_t opcodes[] =
    {
        0000403,  // BR
        0001003,  // BNE
        0001403,  // BEQ
        0002003,  // BGE
        0002403,  // BLT
        0003003,  // BGT
        0003403,  // BLE
        0100003,  // BPL
        0100403,  // BMI
        0101003,  // BHI
        0101403,  // BLOS
        0102003,  // BVC
        0102403,  // BVS
        0103003,  // BHIS / BCC
        0103403,  // BLO / BCS
    };
    for (size_t i = 0; i < sizeof(opcodes) / sizeof(opcodes[0]); i++)
    {
        wchar_t msg[40];
        std::swprintf(msg, std::size(msg), L"opcode %06o", (unsigned int) opcodes[i]);
        uint16_t memory[2] = { opcodes[i], 0 };
        const auto delta = Disasm_CheckForJump(memory, 01000);
        ASSERT_TRUE(delta.has_value()) << msg;
        ASSERT_EQ(4, *delta) << msg;
    }
}

TEST(DisasmTests, Disasm_CheckForJumpSobAndRelativeEdges)
{
    static const JumpCase cases[] =
    {
        { { 0077177 },          -62 },     // SOB nn=63, the longest jump back
        { { 0000167, 0177774 },   0 },     // JMP to itself: X = -4
        { { 0000167, 0177772 },  -1 },     // X = -6
        { { 0000167, 0100000 }, -16382 },  // X = -32768
        { { 0004767, 0000000 },   2 },     // CALL to the next instruction
    };
    CheckJumpCases(cases, sizeof(cases) / sizeof(cases[0]));
}

TEST(DisasmTests, Disasm_CheckForJumpJsrOtherRegisters)
{
    // JSR with a link register other than PC and the relative addressing is a static jump like CALL
    static const JumpCase cases[] =
    {
        { { 0004067, 0000000 },  2 },   // JSR R0, next
        { { 0004367, 0000000 },  2 },   // JSR R3, next
        { { 0004567, 0000010 },  6 },   // JSR R5, +8 bytes: (8 + 4) / 2
        { { 0004567, 0177770 }, -2 },   // JSR R5, back by 8 bytes
    };
    CheckJumpCases(cases, sizeof(cases) / sizeof(cases[0]));
}

TEST(DisasmTests, Disasm_CheckForJumpAbsolute)
{
    // JMP and CALL with an absolute address: the distance is computed from the address of the instruction
    static const AbsoluteJumpCase cases[] =
    {
        { 01000, { 0000137, 01100 },  32 },   // JMP @#1100: (01100 - 01000) / 2
        { 01000, { 0004737, 01100 },  32 },   // CALL @#1100
        { 01000, { 0004537, 00500 }, -96 },   // JSR R5,@#500: (0500 - 01000) / 2
        { 01000, { 0000137, 00776 },  -1 },   // JMP @#776: the previous word
        { 01000, { 0000137, 01002 },   1 },   // JMP @#1002: the next word
    };
    CheckAbsoluteJumpCases(cases, sizeof(cases) / sizeof(cases[0]));
}

TEST(DisasmTests, Disasm_CheckForJumpAbsoluteOddTargetHasNoArrow)
{
    // An odd address is not a valid target of a jump: no arrow
    static const uint16_t cases[][2] =
    {
        { 0000137, 01001 },  // JMP @#1001
        { 0004737, 01001 },  // CALL @#1001
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
    {
        wchar_t msg[40];
        std::swprintf(msg, std::size(msg), L"opcode %06o", (unsigned int) cases[i][0]);
        ASSERT_FALSE(Disasm_CheckForJump(cases[i], 01000).has_value()) << msg;
    }
}

TEST(DisasmTests, Disasm_CheckForJumpRelativeOddTargetHasNoArrow)
{
    // The instruction address is even, so an odd offset gives an odd target: no arrow, as for the absolute form
    static const uint16_t cases[][2] =
    {
        { 0000167, 0000001 },  // JMP 1(PC)
        { 0004767, 0000003 },  // CALL 3(PC)
        { 0004567, 0177777 },  // JSR R5,-1(PC)
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
    {
        wchar_t msg[40];
        std::swprintf(msg, std::size(msg), L"opcode %06o", (unsigned int) cases[i][0]);
        ASSERT_FALSE(Disasm_CheckForJump(cases[i], 01000).has_value()) << msg;
    }
}

TEST(DisasmTests, Disasm_CheckForJumpNotAJump)
{
    static const uint16_t cases[] =
    {
        0000000,  // HALT
        0004000,  // JSR R0,R0: a register is not a target
        0004511,  // JSR R5,(R1): the target depends on a register
        0000111,  // JMP (R1)
        0000121,  // JMP (R1)+
        0000177,  // JMP @X(PC): the target is in memory
        0010001,  // MOV
        0104000,  // EMT
        0104400,  // TRAP
        0005000,  // CLR
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
    {
        wchar_t msg[40];
        std::swprintf(msg, std::size(msg), L"opcode %06o", (unsigned int) cases[i]);
        uint16_t memory[2] = { cases[i], 0 };
        ASSERT_FALSE(Disasm_CheckForJump(memory, 01000).has_value()) << msg;
    }
}

TEST(DisasmTests, Disasm_FloatToStringNumbers)
{
    struct FloatCase { uint16_t high; uint16_t low; const wchar_t* expected; };
    static const FloatCase cases[] =
    {
        { 0000000, 0000000, L"0" },
        { 0040200, 0000000, L"1" },
        { 0040400, 0000000, L"2" },
        { 0040500, 0000000, L"3" },
        { 0040600, 0000000, L"4" },
        { 0040640, 0000000, L"5" },
        { 0041040, 0000000, L"10" },
        { 0041310, 0000000, L"25" },
        { 0040511, 0007733, L"3.1415927" },   // PI
        { 0040311, 0007733, L"1.5707964" },    // PI/2
        { 0040206, 0005222, L"1.0471976" },    // PI/3
        { 0040111, 0007733, L"0.7853982" },  // PI/4
        { 0040006, 0005222, L"0.5235988" },  // PI/6
        { 0040265, 0002363, L"1.4142135" },   // sqrt(2)
        { 0040455, 0174124, L"2.7182817" },   // E
        { 0040317, 0015675, L"1.618034" },   // the golden ratio
        { 0041034, 0164012, L"9.80665" },   // G, the standard gravity
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
    {
        wchar_t msg[40];
        std::swprintf(msg, std::size(msg), L"float %06o %06o", (unsigned int) cases[i].high, (unsigned int) cases[i].low);
        const auto value = Disasm_FloatToString(cases[i].high, cases[i].low);
        ASSERT_STREQ(cases[i].expected, value.c_str()) << msg;
    }
}

TEST(DisasmTests, Disasm_FloatToStringLimits)
{
    struct FloatCase { uint16_t high; uint16_t low; const wchar_t* expected; };
    static const FloatCase cases[] =
    {
        { 0041020, 0000000, L"9" },            // the example from the manual
        { 0141020, 0000000, L"-9" },           // sign bit
        { 0100000, 0000000, L"0" },            // zero exponent is real zero whatever the sign and mantissa
        { 0000177, 0177777, L"0" },
        { 0000200, 0000000, L"2.938736e-39" },  // the smallest number
        { 0100200, 0000000, L"-2.938736e-39" },
        { 0000600, 0000000, L"1.1754944e-38" },  // exponent 3, the smallest one the bit trick handles
        { 0000400, 0000000, L"5.877472e-39" },  // exponent 2, IEEE denormal
        { 0077777, 0177777, L"1.7014117e+38" },  // the largest number
        { 0177777, 0177777, L"-1.7014117e+38" },
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
    {
        wchar_t msg[40];
        std::swprintf(msg, std::size(msg), L"float %06o %06o", (unsigned int) cases[i].high, (unsigned int) cases[i].low);
        const auto value = Disasm_FloatToString(cases[i].high, cases[i].low);
        ASSERT_STREQ(cases[i].expected, value.c_str()) << msg;
    }
}
