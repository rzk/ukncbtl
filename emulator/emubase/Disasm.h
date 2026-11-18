/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

class CProcessor;
class CMemoryController;

/// \brief Disassemble one instruction of KM1801VM2 processor
/// \param[in]  memory Instruction words; missing extension words are treated as zero
/// \param[in]  addr    Address of the instruction
struct DisassembledInstruction
{
    std::wstring mnemonic;
    std::wstring argument;
    uint16_t length = 0;
};

/// \return The decoded instruction, or no value when memory is empty
[[nodiscard]] std::optional<DisassembledInstruction> DisassembleInstruction(
    std::span<const uint16_t> memory, uint16_t addr);

/// \brief Check if the instruction is a jump with a known target, for the jump arrow
/// \param[in]  memory   Memory image: the instruction and the word after it (the offset or the address of the target)
/// \param[in]  address  Address of the instruction
/// \return Distance in words to the known target, or no value
[[nodiscard]] std::optional<int> Disasm_CheckForJump(std::span<const uint16_t> memory, uint16_t address);

// Hints of the current instruction: up to three lines of text and the jump prediction
inline constexpr auto DISASM_HINT_LINES = 3;

struct DisasmHint
{
    std::array<std::wstring, DISASM_HINT_LINES> lines{};
    int count = 0;
    bool isJump = false;
    bool jumpPredict = false;
};

/// \brief Prepare the hints of the instruction at PC
/// \param[in]  memory   Memory image: the instruction and the two words after it
/// \param[in]  processor Processor, the instruction is at its PC
/// \param[in]  memoryController Memory controller of the processor
/// \return The hint text and jump prediction
[[nodiscard]] DisasmHint Disasm_GetHint(std::span<const uint16_t> memory, const CProcessor& processor,
                                        const CMemoryController& memoryController);

/// Prepare the regular instruction hint independently of the jump hint.
[[nodiscard]] DisasmHint Disasm_GetInstructionHint(std::span<const uint16_t> memory,
    const CProcessor& processor, const CMemoryController& memoryController);

struct DisasmJumpHint
{
    std::wstring text;
    bool predict = false;
};
/// Prepare the jump condition and its prediction.
[[nodiscard]] DisasmJumpHint Disasm_GetJumpConditionHint(std::span<const uint16_t> memory,
    const CProcessor& processor, const CMemoryController& memoryController);

/// \brief Convert two words of a FIS floating point number (DEC single precision) to a string
/// \param[in]  high        High word: sign, 8-bit exponent (bias 128), high 7 bits of the mantissa
/// \param[in]  low         Low word: low 16 bits of the mantissa
/// \return Shortest representation (up to nine digits) preserving the 24-bit mantissa
[[nodiscard]] std::wstring Disasm_FloatToString(uint16_t high, uint16_t low);
