#pragma once

#include "arm/arm_instruction.h"
#include <cstdint>
#include <span>
#include <vector>

namespace descomp::arm {

class ThumbDecoder {
public:
    static ARMInstruction decode(uint16_t raw_instruction, uint32_t address, uint16_t next_raw = 0);
    static std::vector<ARMInstruction> decode_buffer(std::span<const uint8_t> buffer, uint32_t base_address);
};

} // namespace descomp::arm
