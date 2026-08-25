#pragma once

#include "ir/ir_instruction.h"
#include <cstdint>
#include <string>
#include <vector>
#include <map>

namespace descomp::ir {

// IR counterpart of a Phase-1 basic block. Contains lowered IR instructions
// while still carrying the original addresses for provenance.
struct IRBasicBlock {
    uint32_t start{0};   // Original start address of the block
    uint32_t end{0};     // Exclusive end address (start + total instruction bytes)

    bool is_thumb{false};

    std::vector<IRInstruction> instructions;

    std::vector<uint32_t> successors;     // Static successor block addresses (0 if unknown)
    std::vector<uint32_t> predecessors;

    bool is_entry{false};
    bool is_exit{false};
    bool has_indirect_jump{false};
    bool has_unknown_target{false};

    [[nodiscard]] size_t instruction_count() const { return instructions.size(); }
};

// IR counterpart of a Phase-1 function.
struct IRFunction {
    uint32_t address{0};
    std::string name;

    bool thumb{false};
    uint32_t size{0};

    std::vector<uint32_t> blocks;          // Block start addresses (in order)
    std::map<uint32_t, IRBasicBlock> block_map; // start -> block

    std::vector<uint32_t> callees;
    std::vector<uint32_t> callers;

    std::vector<uint32_t> indirect_branches;
    std::vector<uint32_t> indirect_calls;
    std::vector<uint32_t> unknown_targets;

    [[nodiscard]] size_t instruction_count() const {
        size_t n = 0;
        for (const auto& [_, b] : block_map) n += b.instructions.size();
        return n;
    }
};

// A complete lowered program: a collection of IR functions.
struct IRProgram {
    std::map<uint32_t, IRFunction> functions;

    [[nodiscard]] size_t function_count() const { return functions.size(); }
    [[nodiscard]] size_t instruction_count() const {
        size_t n = 0;
        for (const auto& [_, f] : functions) n += f.instruction_count();
        return n;
    }
    [[nodiscard]] size_t block_count() const {
        size_t n = 0;
        for (const auto& [_, f] : functions) n += f.block_map.size();
        return n;
    }
};

} // namespace descomp::ir
