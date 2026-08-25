#pragma once

#include "ir/ir_instruction.h"
#include "ir/ir_block.h"
#include "runtime/nds_runtime.h"
#include <unordered_map>
#include <cstdio>
#include <vector>
#include <deque>
#include <string>

namespace descomp::ir {

// Forward declaration
struct IRProgram;

struct ExecStats {
    uint64_t instructions_executed{0};
    uint64_t unknown_encountered{0};
    uint64_t unimpl_encountered{0};
    uint64_t arm_executed{0};
    uint64_t thumb_executed{0};
    uint64_t functions_executed{0};
    uint64_t blocks_executed{0};
    uint64_t bl_calls{0};
    uint64_t bx_returns{0};
    bool stopped{false};
    uint32_t stop_address{0};
    std::string stop_reason;
};

struct DeepTraceEntry {
    uint32_t pc{0};
    uint32_t raw{0};
    bool thumb{false};
    uint32_t func{0};
    uint32_t block{0};
    std::string mnemonic;
    uint32_t r[16]{};
    uint32_t cpsr{0};
    bool has_mem_read{false};
    uint32_t mem_read_addr{0}, mem_read_val{0}, mem_read_size{0};
    bool has_mem_write{false};
    uint32_t mem_write_addr{0}, mem_write_val{0}, mem_write_size{0};
    bool is_branch{false};
    bool branch_taken{false};
    std::string cond_str;
};

class IRInterpreter {
public:
    IRInterpreter();

    void init(runtime::CPUState& cpu);
    void set_program(const IRProgram& prog);
    ExecStats execute(const IRFunction& fn, uint64_t max_instructions);
    void trace_instruction(uint32_t pc, bool thumb, const std::string& disasm,
                           uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3,
                           uint32_t sp, uint32_t lr);

    bool trace_enabled{false};
    uint64_t trace_limit{200};
    uint64_t trace_count{0};

    // Deep trace circular buffer (last N instructions, detailed)
    bool deep_trace_enabled{false};
    size_t deep_trace_limit{5000};
    std::deque<DeepTraceEntry> deep_trace;
    void clear_deep_trace() { deep_trace.clear(); }
    const std::deque<DeepTraceEntry>& get_deep_trace() const { return deep_trace; }
    void push_deep_trace(const DeepTraceEntry& e) {
        if (!deep_trace_enabled) return;
        if (deep_trace.size() >= deep_trace_limit) deep_trace.pop_front();
        deep_trace.push_back(e);
    }

private:
    runtime::CPUState* cpu_{nullptr};
    IRProgram* program_{nullptr};
    uint32_t temps_[256]{};

    uint32_t resolve(const IRValue& v);
    void write_dest(const IRValue& d, uint32_t val);
    uint32_t compute_memory_address(const IRMemory& mem);
    bool eval_condition(arm::Condition cond);

    uint32_t shift_val(uint32_t val, arm::ShiftType type, uint32_t amount, bool& carry_out);
    ExecStats execute_function(uint32_t func_addr, uint64_t max_instructions, uint64_t& global_count);
};

} // namespace descomp::ir
