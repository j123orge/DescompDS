#pragma once

#include "ir/ir_instruction.h"
#include "ir/ir_block.h"

#include <string>
#include <sstream>

namespace descomp::lifter {

// Low-level C++ emitter. Generates compilable C++ text from descomp IR.
// All memory accesses are routed through the HAL and no DS address is
// dereferenced directly. Each emitted instruction is preceded by a
// provenance comment containing its original address and raw encoding.
class CppEmitter {
public:
    CppEmitter() = default;

    // Prelude: includes, HAL forward declarations, helper inlines.
    // Makes the generated translation unit self-contained and compilable.
    void emit_prelude();
    void emit_header_guard(const std::string& guard_name = "");

    // Emit a single IR instruction.
    void emit_instruction(const ir::IRInstruction& inst);

    // Emit an IR basic block (label + instructions + terminator).
    void emit_block(const ir::IRBasicBlock& block);

    // Emit a complete IR function (C++ function with labels/gotos).
    void emit_function(const ir::IRFunction& fn);

    // Emit an entire IR program (prelude + all functions).
    void emit_program(const ir::IRProgram& prog);

    // Retrieve generated text.
    [[nodiscard]] std::string str() const { return m_out.str(); }
    void clear();

    // Helpers exposed for testing.
    [[nodiscard]] static std::string value_expr(const ir::IRValue& v);
    [[nodiscard]] static std::string reg_expr(uint8_t reg);
    [[nodiscard]] static std::string imm_expr(int64_t imm);
    [[nodiscard]] static std::string flag_expr(ir::IRFlag f);
    [[nodiscard]] static std::string temp_expr(uint32_t id);
    [[nodiscard]] static std::string condition_expr(arm::Condition cond);
    [[nodiscard]] static std::string mem_addr_expr(const ir::IRMemory& m);

private:
    std::ostringstream m_out;
    int m_indent{0};

    void emit_line(const std::string& line);
    void emit_raw(const std::string& text);
    void indent_inc() { ++m_indent; }
    void indent_dec() { if (m_indent > 0) --m_indent; }
    [[nodiscard]] std::string indent_str() const;

    void emit_condition_wrapper(const std::optional<arm::Condition>& cond,
                                const ir::IRInstruction& inst,
                                const std::string& body_indent_extra = "");

    // Per-opcode emitters.
    void emit_mov(const ir::IRInstruction& inst);
    void emit_arith(const ir::IRInstruction& inst);
    void emit_logic(const ir::IRInstruction& inst);
    void emit_cmp(const ir::IRInstruction& inst);
    void emit_shift(const ir::IRInstruction& inst);
    void emit_mul(const ir::IRInstruction& inst);
    void emit_load(const ir::IRInstruction& inst);
    void emit_store(const ir::IRInstruction& inst);
    void emit_branch(const ir::IRInstruction& inst);
    void emit_cond_branch(const ir::IRInstruction& inst);
    void emit_call(const ir::IRInstruction& inst);
    void emit_return(const ir::IRInstruction& inst);
    void emit_indirect_branch(const ir::IRInstruction& inst);
    void emit_unknown(const ir::IRInstruction& inst);
    void emit_nop(const ir::IRInstruction& inst);
    void emit_clz(const ir::IRInstruction& inst);
    void emit_mrs(const ir::IRInstruction& inst);
    void emit_msr(const ir::IRInstruction& inst);
    void emit_swi(const ir::IRInstruction& inst);
    void emit_bkpt(const ir::IRInstruction& inst);
    void emit_long_mul(const ir::IRInstruction& inst);

    void emit_flags_update(const ir::IRInstruction& inst, const std::string& result_var);
};

} // namespace descomp::lifter
