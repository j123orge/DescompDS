#pragma once

#include "arm/arm_instruction.h"
#include <cstdint>
#include <vector>
#include <string>
#include <span>
#include <map>

namespace descomp::analysis {

struct BasicBlock {
    uint32_t start{0};
    uint32_t end{0}; // Exclusive address: address of last instruction + size

    std::vector<arm::ARMInstruction> instructions;

    std::vector<uint32_t> successors;
    std::vector<uint32_t> predecessors;

    bool is_entry{false};
    bool is_exit{false};
    bool has_indirect_jump{false};
    bool has_unknown_target{false};

    [[nodiscard]] size_t instruction_count() const { return instructions.size(); }
    [[nodiscard]] std::string to_json(int indent = 2) const;
};

class BasicBlockBuilder {
public:
    static std::map<uint32_t, BasicBlock> build_blocks(
        std::span<const arm::ARMInstruction> instructions,
        const std::vector<uint32_t>& known_entry_points = {}
    );
};

} // namespace descomp::analysis
