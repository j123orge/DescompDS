#include "arm/arm_registers.h"
#include <array>

namespace descomp::arm {

static constexpr std::array<std::string_view, 16> kRegisterNames = {
    "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7",
    "r8", "r9", "r10", "r11", "r12", "sp", "lr", "pc"
};

std::string_view register_name(uint8_t reg) {
    if (reg < 16) {
        return kRegisterNames[reg];
    }
    return "unknown_reg";
}

std::string_view shift_name(ShiftType shift) {
    switch (shift) {
        case ShiftType::LSL: return "LSL";
        case ShiftType::LSR: return "LSR";
        case ShiftType::ASR: return "ASR";
        case ShiftType::ROR: return "ROR";
        case ShiftType::RRX: return "RRX";
        default: return "UNKNOWN_SHIFT";
    }
}

} // namespace descomp::arm
