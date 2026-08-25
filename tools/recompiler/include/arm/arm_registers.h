#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace descomp::arm {

enum Register : uint8_t {
    R0 = 0,
    R1 = 1,
    R2 = 2,
    R3 = 3,
    R4 = 4,
    R5 = 5,
    R6 = 6,
    R7 = 7,
    R8 = 8,
    R9 = 9,
    R10 = 10,
    R11 = 11,
    R12 = 12,
    R13 = 13,
    SP = 13,
    R14 = 14,
    LR = 14,
    R15 = 15,
    PC = 15,
    REG_NONE = 0xFF
};

enum class ShiftType : uint8_t {
    LSL = 0, // Logical Shift Left
    LSR = 1, // Logical Shift Right
    ASR = 2, // Arithmetic Shift Right
    ROR = 3, // Rotate Right
    RRX = 4  // Rotate Right with Extend
};

std::string_view register_name(uint8_t reg);
std::string_view shift_name(ShiftType shift);

} // namespace descomp::arm
