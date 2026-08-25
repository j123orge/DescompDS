#pragma once

#include "ir/ir_instruction.h"
#include "ir/ir_block.h"
#include "arm/arm_instruction.h"
#include "analysis/basic_block.h"
#include "analysis/function.h"

#include <vector>
#include <map>

namespace descomp::ir {

// Translates Phase-1 decoded ARM/Thumb instructions into the DescompDS IR.
//
// Pipeline:
//   arm::ARMInstruction  ->  std::vector<IRInstruction>
//   analysis::BasicBlock ->  IRBasicBlock
//   analysis::Function   ->  IRFunction
//   (blocks + functions) ->  IRProgram
//
// The translator never emits target-ISA code; it only produces IR.
class IRTranslator {
public:
    // Lower a single decoded instruction into one or more IR instructions.
    // A single ARM instruction may expand to multiple IR ops (e.g. when a
    // shifted register operand is involved).
    static std::vector<IRInstruction> lift(const arm::ARMInstruction& inst);

    // Lower a Phase-1 basic block, preserving addresses, successors and mode.
    static IRBasicBlock lift_block(const analysis::BasicBlock& bb);

    // Lower a Phase-1 function, preserving structure and call graph edges.
    static IRFunction lift_function(
        const analysis::Function& fn,
        const std::map<uint32_t, analysis::BasicBlock>& blocks);

    // Lower a whole program (blocks + discovered functions).
    static IRProgram lift_program(
        const std::map<uint32_t, analysis::BasicBlock>& blocks,
        const std::map<uint32_t, analysis::Function>& functions);

private:
    // Helpers used while lifting a single instruction.
    static IRValue build_shift_operand(
        const arm::ARMInstruction& inst,
        std::vector<IRInstruction>& out,
        uint32_t& temp_counter);

    static IRMemory build_memory_operand(const arm::ARMInstruction& inst);
};

} // namespace descomp::ir
