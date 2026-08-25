#pragma once

#include "arm/arm_instruction.h"
#include <cstdint>
#include <span>
#include <vector>

namespace descomp::arm {

class ARMDecoder {
public:
    static ARMInstruction decode(uint32_t raw_instruction, uint32_t address);
    static std::vector<ARMInstruction> decode_buffer(std::span<const uint8_t> buffer, uint32_t base_address);

private:
    static ARMInstruction decode_branch(uint32_t raw, uint32_t address, Condition cond);
    static ARMInstruction decode_data_processing(uint32_t raw, uint32_t address, Condition cond);
    static ARMInstruction decode_single_data_transfer(uint32_t raw, uint32_t address, Condition cond);
    static ARMInstruction decode_halfword_transfer(uint32_t raw, uint32_t address, Condition cond);
    static ARMInstruction decode_block_transfer(uint32_t raw, uint32_t address, Condition cond);
    static ARMInstruction decode_multiply(uint32_t raw, uint32_t address, Condition cond);
    static ARMInstruction decode_misc(uint32_t raw, uint32_t address, Condition cond);
};

} // namespace descomp::arm
