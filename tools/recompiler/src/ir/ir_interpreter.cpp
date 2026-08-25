#include "ir/ir_interpreter.h"
#include "ir/ir_instruction.h"
#include "runtime/nds_runtime.h"
#include "runtime/dynamic_code.h"
#include <cstring>
#include <cstdio>

namespace descomp::ir {

IRInterpreter::IRInterpreter() {
    std::memset(temps_, 0, sizeof(temps_));
}

void IRInterpreter::init(runtime::CPUState& cpu) {
    cpu_ = &cpu;
    std::memset(temps_, 0, sizeof(temps_));
    trace_count = 0;
}

void IRInterpreter::set_program(const IRProgram& prog) {
    program_ = const_cast<IRProgram*>(&prog);
}

uint32_t IRInterpreter::resolve(const IRValue& v) {
    switch (v.kind) {
        case IRValueKind::Register:
            return cpu_->r[v.id];
        case IRValueKind::Immediate:
            return static_cast<uint32_t>(v.immediate);
        case IRValueKind::Temporary:
            return temps_[v.id];
        case IRValueKind::Flag: {
            auto f = static_cast<IRFlag>(v.id);
            switch (f) {
                case IRFlag::N: return cpu_->flag_n() ? 1 : 0;
                case IRFlag::Z: return cpu_->flag_z() ? 1 : 0;
                case IRFlag::C: return cpu_->flag_c() ? 1 : 0;
                case IRFlag::V: return cpu_->flag_v() ? 1 : 0;
            }
            return 0;
        }
        case IRValueKind::Memory:
            return 0; // should use compute_memory_address + read instead
    }
    return 0;
}

void IRInterpreter::write_dest(const IRValue& d, uint32_t val) {
    switch (d.kind) {
        case IRValueKind::Register:
            cpu_->r[d.id] = val;
            break;
        case IRValueKind::Temporary:
            temps_[d.id] = val;
            break;
        default:
            break;
    }
}

uint32_t IRInterpreter::compute_memory_address(const IRMemory& mem) {
    uint32_t base = cpu_->r[mem.base_reg];
    uint32_t offset = static_cast<uint32_t>(mem.displacement);
    if (mem.index_reg != arm::REG_NONE) {
        uint32_t idx = cpu_->r[mem.index_reg];
        bool c = cpu_->flag_c();
        idx = shift_val(idx, mem.index_shift, mem.index_shift_amount, c);
        if (mem.add_index) offset += idx;
        else offset = (offset >= idx) ? offset - idx : 0; // for register offset, displacement is 0, so offset = +/- idx
        // Actually for register offset, displacement is 0, so offset is just idx with sign
        // Recompute correctly: if index_reg present, offset is idx (with sign), ignore displacement
        // For simplicity, if index_reg present, offset = idx with sign
        if (mem.index_reg != arm::REG_NONE) {
            // offset already includes displacement + idx, but for register offset displacement is 0
            // So we need to handle: offset = (add? idx : -idx) when index present
            // The above logic of offset += idx is wrong when displacement is 0 and we do offset = displacement +/- idx
            // Let's recompute: if index present, offset = idx with sign, else displacement with sign
        }
    }
    // Correct handling: if pre_indexed, address = base +/- offset, else address = base
    if (mem.pre_indexed) {
        if (mem.add_index) {
            // For register offset, offset already is idx, for immediate offset, offset is displacement
            // Need to distinguish: if index_reg != REG_NONE, offset is idx (already computed with sign via add_index)
            // So just return base + (add? offset : -offset) but offset already signed?
            // Simplify: if index present, offset is already signed via add_index, so return base + offset
            // If no index, offset is displacement, and add_index indicates add/sub
            if (mem.index_reg != arm::REG_NONE) {
                return base + offset;
            } else {
                return mem.add_index ? base + offset : base - offset;
            }
        } else {
            // add_index false means subtract
            if (mem.index_reg != arm::REG_NONE) {
                return base - offset;
            } else {
                return base - offset;
            }
        }
    } else {
        // post-indexed: address is just base
        return base;
    }
}

bool IRInterpreter::eval_condition(arm::Condition cond) {
    switch (cond) {
        case arm::Condition::EQ: return cpu_->flag_z();
        case arm::Condition::NE: return !cpu_->flag_z();
        case arm::Condition::CS: return cpu_->flag_c();
        case arm::Condition::CC: return !cpu_->flag_c();
        case arm::Condition::MI: return cpu_->flag_n();
        case arm::Condition::PL: return !cpu_->flag_n();
        case arm::Condition::VS: return cpu_->flag_v();
        case arm::Condition::VC: return !cpu_->flag_v();
        case arm::Condition::HI: return cpu_->flag_c() && !cpu_->flag_z();
        case arm::Condition::LS: return !cpu_->flag_c() || cpu_->flag_z();
        case arm::Condition::GE: return cpu_->flag_n() == cpu_->flag_v();
        case arm::Condition::LT: return cpu_->flag_n() != cpu_->flag_v();
        case arm::Condition::GT: return !cpu_->flag_z() && (cpu_->flag_n() == cpu_->flag_v());
        case arm::Condition::LE: return cpu_->flag_z() || (cpu_->flag_n() != cpu_->flag_v());
        case arm::Condition::AL: return true;
        case arm::Condition::NV: return false;
    }
    return true;
}

uint32_t IRInterpreter::shift_val(uint32_t val, arm::ShiftType type, uint32_t amount, bool& carry_out) {
    if (amount == 0) return val;
    switch (type) {
        case arm::ShiftType::LSL:
            if (amount >= 32) { carry_out = false; return 0; }
            carry_out = (val >> (32 - amount)) & 1;
            return val << amount;
        case arm::ShiftType::LSR:
            if (amount >= 32) { carry_out = false; return 0; }
            carry_out = (val >> (amount - 1)) & 1;
            return val >> amount;
        case arm::ShiftType::ASR:
            if (amount >= 32) { carry_out = (val >> 31) & 1; return (val >> 31) ? 0xFFFFFFFF : 0; }
            carry_out = (val >> (amount - 1)) & 1;
            return static_cast<uint32_t>(static_cast<int32_t>(val) >> amount);
        case arm::ShiftType::ROR: {
            amount &= 31;
            if (amount == 0) { carry_out = (val >> 31) & 1; return val; }
            uint32_t result = (val >> amount) | (val << (32 - amount));
            carry_out = (result >> 31) & 1;
            return result;
        }
    }
    return val;
}

void IRInterpreter::trace_instruction(uint32_t pc, bool thumb, const std::string& disasm,
                                       uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3,
                                       uint32_t sp, uint32_t lr) {
    if (!trace_enabled) return;
    if (trace_count >= trace_limit) return;
    const char* mode = thumb ? "Thumb" : "ARM";
    std::printf("PC=%08X %-5s %-30s R0=%08X R1=%08X R2=%08X R3=%08X SP=%08X LR=%08X\n",
                pc, mode, disasm.c_str(), r0, r1, r2, r3, sp, lr);
    ++trace_count;
}

ExecStats IRInterpreter::execute(const IRFunction& fn, uint64_t max_instructions) {
    uint64_t count = 0;
    return execute_function(fn.address, max_instructions, count);
}

ExecStats IRInterpreter::execute_function(uint32_t func_addr, uint64_t max_instructions, uint64_t& global_count) {
    ExecStats stats;
    if (!cpu_) {
        stats.stopped = true;
        stats.stop_reason = "no cpu";
        return stats;
    }

    // Find function - if not found, try to execute linearly from this address
    const IRFunction* fn_ptr = nullptr;
    if (program_) {
        auto it = program_->functions.find(func_addr);
        if (it != program_->functions.end()) fn_ptr = &it->second;
    }
    if (!fn_ptr) {
        // No function at this address - try to find any function containing this address
        if (program_) {
            for (auto& [addr, fn] : program_->functions) {
                if (addr <= func_addr) {
                    // Check if func_addr falls within this function's address range
                    for (auto& blk : fn.blocks) {
                        if (blk == func_addr) {
                            fn_ptr = &fn;
                            break;
                        }
                    }
                    if (fn_ptr) break;
                }
            }
        }
        if (!fn_ptr) {
            // Try dynamic code (ITCM/DTCM) - J1
            // Use improved DynamicCodeManager::ensure_function which handles
            // decode, register, and generation invalidation
            bool is_itcm = (func_addr < 0x00008000);
            bool is_dtcm = (func_addr >= 0x027E0000 && func_addr < 0x027E4000);
            if ((is_itcm || is_dtcm) && runtime::DynamicCodeManager::instance().ensure_function(func_addr, false)) {
                // ensure_function decoded and registered the function
                // Look it up in the program
                auto dyn_it = program_->functions.find(func_addr);
                if (dyn_it != program_->functions.end()) fn_ptr = &dyn_it->second;
            }
        }
        if (!fn_ptr) {
            stats.stopped = true;
            stats.stop_reason = "function not found at " + std::to_string(func_addr);
            stats.stop_address = func_addr;
            return stats;
        }
    }

    const IRFunction& fn = *fn_ptr;
    stats.functions_executed = 1;

    uint32_t func_addr_actual = fn.address;
    // Set PC to function entry
    cpu_->r[15] = func_addr_actual + (fn.thumb ? 4 : 8);

    // Build a map of block addresses for quick lookup (mutable for cross-function jumps)
    std::unordered_map<uint32_t, const IRBasicBlock*> current_block_map;
    for (auto& [addr, blk] : fn.block_map) {
        current_block_map[addr] = &blk;
    }

    // Linear block walk - execute blocks in order, follow branches
    // Start from the first block in function order
    uint32_t current_block_addr = fn.blocks.empty() ? 0 : fn.blocks[0];

    while (stats.instructions_executed < max_instructions) {
        auto it = current_block_map.find(current_block_addr);
        if (it == current_block_map.end()) {
            stats.stopped = true;
            stats.stop_reason = "block not found at " + std::to_string(current_block_addr);
            stats.stop_address = current_block_addr;
            break;
        }

        const IRBasicBlock* blk = it->second;
        stats.blocks_executed++;

        bool block_branch_taken = false;
        uint32_t next_block = 0;

        for (const auto& inst : blk->instructions) {
            if (global_count >= max_instructions) {
                stats.stopped = true;
                stats.stop_reason = "instruction limit reached";
                stats.stop_address = inst.address;
                break;
            }
            ++global_count;
            ++stats.instructions_executed;
            if (inst.is_thumb) ++stats.thumb_executed; else ++stats.arm_executed;

            // Trace
            if (trace_enabled && trace_count < trace_limit) {
                std::string disasm = inst.mnemonic;
                for (auto& op : inst.operands) {
                    if (op.kind == IRValueKind::Immediate) {
                        char buf[32];
                        std::snprintf(buf, sizeof(buf), " 0x%llX", static_cast<unsigned long long>(op.immediate));
                        disasm += buf;
                    } else if (op.kind == IRValueKind::Register) {
                        disasm += " R" + std::to_string(op.id);
                    } else if (op.kind == IRValueKind::Temporary) {
                        disasm += " T" + std::to_string(op.id);
                    }
                }
                trace_instruction(inst.address, cpu_->thumb, disasm,
                                  cpu_->r[0], cpu_->r[1], cpu_->r[2], cpu_->r[3],
                                  cpu_->r[13], cpu_->r[14]);
            }

            // Deep trace capture - prepare entry
            DeepTraceEntry deep_entry;
            bool do_deep = deep_trace_enabled;
            if (do_deep) {
                deep_entry.pc = inst.address;
                deep_entry.raw = inst.raw;
                deep_entry.thumb = inst.is_thumb;
                deep_entry.func = func_addr;
                deep_entry.block = current_block_addr;
                deep_entry.mnemonic = inst.mnemonic;
                deep_entry.cpsr = cpu_->cpsr;
                for (int i=0;i<16;++i) deep_entry.r[i]=cpu_->r[i];
                deep_entry.is_branch = inst.is_branch() || inst.op==IROperation::BRANCH || inst.op==IROperation::COND_BRANCH || inst.op==IROperation::CALL || inst.op==IROperation::INDIRECT_BRANCH;
                deep_entry.branch_taken = false;
                if (inst.condition.has_value()) {
                    // store cond string
                    deep_entry.cond_str = std::string(arm::condition_suffix(*inst.condition));
                }
            }

            // Check condition
            bool cond_pass = true;
            if (inst.condition.has_value() && *inst.condition != arm::Condition::AL) {
                cond_pass = eval_condition(*inst.condition);
                if (!cond_pass) {
                    if (do_deep) {
                        // even when not taken, record
                        deep_entry.branch_taken = false;
                        // snapshot after (no change)
                        deep_entry.cpsr = cpu_->cpsr;
                        for (int i=0;i<16;++i) deep_entry.r[i]=cpu_->r[i];
                        push_deep_trace(deep_entry);
                    }
                    continue;
                }
            }

            // For deep trace, mark branch taken if this is a branch and condition passed
            if (do_deep && deep_entry.is_branch) deep_entry.branch_taken = true;

            switch (inst.op) {
                case IROperation::NOP:
                    break;

                case IROperation::MOV:
                case IROperation::MVN: {
                    uint32_t val = resolve(inst.operands[0]);
                    if (inst.op == IROperation::MVN) val = ~val;
                    write_dest(*inst.dest, val);
                    if (inst.update_flags) {
                        cpu_->set_flags((val >> 31) & 1, val == 0, cpu_->flag_c(), cpu_->flag_v());
                    }
                    break;
                }

                case IROperation::ADD:
                case IROperation::ADC: {
                    uint32_t a = resolve(inst.operands[0]);
                    uint32_t b = resolve(inst.operands[1]);
                    uint64_t result64 = static_cast<uint64_t>(a) + b;
                    if (inst.op == IROperation::ADC && cpu_->flag_c()) ++result64;
                    uint32_t result = static_cast<uint32_t>(result64);
                    write_dest(*inst.dest, result);
                    if (inst.update_flags) {
                        bool n = (result >> 31) & 1;
                        bool z = result == 0;
                        bool c = result64 > 0xFFFFFFFF;
                        bool v = (~(a ^ b) & (a ^ result)) >> 31;
                        cpu_->set_flags(n, z, c, v);
                    }
                    break;
                }

                case IROperation::SUB:
                case IROperation::SBC:
                case IROperation::RSB:
                case IROperation::RSC: {
                    uint32_t a, b;
                    if (inst.op == IROperation::RSB || inst.op == IROperation::RSC) {
                        b = resolve(inst.operands[0]);
                        a = resolve(inst.operands[1]);
                    } else {
                        a = resolve(inst.operands[0]);
                        b = resolve(inst.operands[1]);
                    }
                    bool borrow = true;
                    if (inst.op == IROperation::SBC) borrow = cpu_->flag_c();
                    if (inst.op == IROperation::RSC) borrow = cpu_->flag_c();
                    uint64_t result64 = static_cast<uint64_t>(a) - b;
                    if (!borrow) --result64;
                    uint32_t result = static_cast<uint32_t>(result64);
                    write_dest(*inst.dest, result);
                    if (inst.update_flags) {
                        bool n = (result >> 31) & 1;
                        bool z = result == 0;
                        bool c = !borrow ? (static_cast<uint64_t>(a) >= static_cast<uint64_t>(b) + 1) : (static_cast<uint64_t>(a) >= static_cast<uint64_t>(b));
                        bool v = ((a ^ b) & (a ^ result)) >> 31;
                        cpu_->set_flags(n, z, c, v);
                    }
                    break;
                }

                case IROperation::AND:
                case IROperation::ORR:
                case IROperation::EOR:
                case IROperation::BIC: {
                    uint32_t a = resolve(inst.operands[0]);
                    uint32_t b = resolve(inst.operands[1]);
                    uint32_t result;
                    switch (inst.op) {
                        case IROperation::AND: result = a & b; break;
                        case IROperation::ORR:  result = a | b; break;
                        case IROperation::EOR:  result = a ^ b; break;
                        case IROperation::BIC:  result = a & ~b; break;
                        default: result = 0; break;
                    }
                    write_dest(*inst.dest, result);
                    if (inst.update_flags) {
                        bool n = (result >> 31) & 1;
                        bool z = result == 0;
                        cpu_->set_flags(n, z, cpu_->flag_c(), cpu_->flag_v());
                    }
                    break;
                }

                case IROperation::CMP:
                case IROperation::CMN:
                case IROperation::TST:
                case IROperation::TEQ: {
                    uint32_t a = resolve(inst.operands[0]);
                    uint32_t b = resolve(inst.operands[1]);
                    uint32_t result;
                    bool carry = cpu_->flag_c();
                    bool overflow = cpu_->flag_v();
                    switch (inst.op) {
                        case IROperation::CMP: {
                            uint64_t r64 = static_cast<uint64_t>(a) - b;
                            result = static_cast<uint32_t>(r64);
                            carry = (r64 >> 32) == 0;
                            overflow = ((a ^ b) & (a ^ result)) >> 31;
                            break;
                        }
                        case IROperation::CMN: {
                            uint64_t r64 = static_cast<uint64_t>(a) + b;
                            result = static_cast<uint32_t>(r64);
                            carry = r64 > 0xFFFFFFFF;
                            overflow = (~(a ^ b) & (a ^ result)) >> 31;
                            break;
                        }
                        case IROperation::TST: result = a & b; carry = false; break;
                        case IROperation::TEQ: result = a ^ b; carry = false; break;
                        default: result = 0; break;
                    }
                    bool n = (result >> 31) & 1;
                    bool z = result == 0;
                    cpu_->set_flags(n, z, carry, overflow);
                    break;
                }

                case IROperation::LSL:
                case IROperation::LSR:
                case IROperation::ASR:
                case IROperation::ROR: {
                    uint32_t val = resolve(inst.operands[0]);
                    uint32_t amount = resolve(inst.operands[1]);
                    arm::ShiftType stype;
                    switch (inst.op) {
                        case IROperation::LSL: stype = arm::ShiftType::LSL; break;
                        case IROperation::LSR: stype = arm::ShiftType::LSR; break;
                        case IROperation::ASR: stype = arm::ShiftType::ASR; break;
                        case IROperation::ROR: stype = arm::ShiftType::ROR; break;
                        default: stype = arm::ShiftType::LSL; break;
                    }
                    bool c = cpu_->flag_c();
                    uint32_t result = shift_val(val, stype, amount, c);
                    write_dest(*inst.dest, result);
                    if (inst.update_flags) {
                        bool n = (result >> 31) & 1;
                        bool z = result == 0;
                        cpu_->set_flags(n, z, c, cpu_->flag_v());
                    }
                    break;
                }

                case IROperation::MUL: {
                    uint32_t a = resolve(inst.operands[0]);
                    uint32_t b = resolve(inst.operands[1]);
                    uint32_t result = a * b;
                    write_dest(*inst.dest, result);
                    if (inst.update_flags) {
                        cpu_->set_flags((result >> 31) & 1, result == 0, cpu_->flag_c(), cpu_->flag_v());
                    }
                    break;
                }

                case IROperation::UMULL:
                case IROperation::UMLAL:
                case IROperation::SMULL:
                case IROperation::SMLAL: {
                    uint32_t rm = resolve(inst.operands[0]);
                    uint32_t rs = resolve(inst.operands[1]);
                    uint64_t product;
                    if (inst.op == IROperation::SMULL || inst.op == IROperation::SMLAL) {
                        product = static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(rm)) * static_cast<int32_t>(rs));
                    } else {
                        product = static_cast<uint64_t>(rm) * rs;
                    }
                    if (inst.op == IROperation::UMLAL || inst.op == IROperation::SMLAL) {
                        uint32_t rdlo = resolve(*inst.dest);
                        uint32_t rdhi = resolve(*inst.dest2);
                        product += (static_cast<uint64_t>(rdhi) << 32) | rdlo;
                    }
                    write_dest(*inst.dest, static_cast<uint32_t>(product));
                    if (inst.dest2) write_dest(*inst.dest2, static_cast<uint32_t>(product >> 32));
                    if (inst.update_flags) {
                        cpu_->set_flags((product >> 63) & 1, product == 0, cpu_->flag_c(), cpu_->flag_v());
                    }
                    break;
                }

                case IROperation::CLZ: {
                    uint32_t val = resolve(inst.operands[0]);
                    uint32_t count = 0;
                    if (val == 0) count = 32;
                    else { uint32_t tmp = val; while ((tmp >> 31) == 0) { ++count; tmp <<= 1; } }
                    write_dest(*inst.dest, count);
                    break;
                }

                case IROperation::LOAD: {
                    uint32_t addr = compute_memory_address(inst.operands[0].memory);
                    uint32_t val = 0;
                    auto& mem = inst.operands[0].memory;
                    switch (mem.access) {
                        case IRMemoryAccess::Byte:
                            val = runtime::read8(addr);
                            if (mem.sign_extend && (val & 0x80)) val |= 0xFFFFFF00;
                            break;
                        case IRMemoryAccess::HalfWord:
                            val = runtime::read16(addr);
                            if (mem.sign_extend && (val & 0x8000)) val |= 0xFFFF0000;
                            break;
                        case IRMemoryAccess::Word:
                            val = runtime::read32(addr);
                            break;
                    }
                    write_dest(*inst.dest, val);
                    if (do_deep) {
                        deep_entry.has_mem_read = true;
                        deep_entry.mem_read_addr = addr;
                        deep_entry.mem_read_val = val;
                        deep_entry.mem_read_size = (mem.access==IRMemoryAccess::Word?4: mem.access==IRMemoryAccess::HalfWord?2:1);
                    }
                    // Writeback
                    if (mem.writeback) {
                        int32_t delta = mem.pre_indexed
                            ? (mem.add_index ? static_cast<int32_t>(inst.operands[0].memory.displacement) : -static_cast<int32_t>(inst.operands[0].memory.displacement))
                            : (mem.add_index ? static_cast<int32_t>(inst.operands[0].memory.displacement) : -static_cast<int32_t>(inst.operands[0].memory.displacement));
                        cpu_->r[mem.base_reg] = addr + (mem.pre_indexed ? 0 : delta);
                    }
                    break;
                }

                case IROperation::STORE: {
                    uint32_t addr = compute_memory_address(inst.operands[0].memory);
                    uint32_t val = resolve(inst.dest ? *inst.dest : IRValue::imm(0));
                    auto& mem = inst.operands[0].memory;
                    switch (mem.access) {
                        case IRMemoryAccess::Byte:
                            runtime::write8(addr, static_cast<uint8_t>(val));
                            break;
                        case IRMemoryAccess::HalfWord:
                            runtime::write16(addr, static_cast<uint16_t>(val));
                            break;
                        case IRMemoryAccess::Word:
                            runtime::write32(addr, val);
                            break;
                    }
                    if (do_deep) {
                        deep_entry.has_mem_write = true;
                        deep_entry.mem_write_addr = addr;
                        deep_entry.mem_write_val = val;
                        deep_entry.mem_write_size = (mem.access==IRMemoryAccess::Word?4: mem.access==IRMemoryAccess::HalfWord?2:1);
                    }
                    if (mem.writeback) {
                        int32_t delta = mem.pre_indexed
                            ? (mem.add_index ? static_cast<int32_t>(inst.operands[0].memory.displacement) : -static_cast<int32_t>(inst.operands[0].memory.displacement))
                            : (mem.add_index ? static_cast<int32_t>(inst.operands[0].memory.displacement) : -static_cast<int32_t>(inst.operands[0].memory.displacement));
                        cpu_->r[mem.base_reg] = addr + (mem.pre_indexed ? 0 : delta);
                    }
                    break;
                }

                case IROperation::BRANCH: {
                    uint32_t target = resolve(inst.operands[0]);
                    // Find the target block
                    auto target_it = current_block_map.find(target);
                    if (target_it != current_block_map.end()) {
                        next_block = target;
                        block_branch_taken = true;
                    } else {
                        // Branch outside function - return to caller
                        stats.stopped = true;
                        stats.stop_reason = "branch outside function";
                        stats.stop_address = target;
                        return stats;
                    }
                    break;
                }

                case IROperation::COND_BRANCH: {
                    uint32_t target = resolve(inst.operands[0]);
                    auto target_it = current_block_map.find(target);
                    if (target_it != current_block_map.end()) {
                        next_block = target;
                        block_branch_taken = true;
                    } else {
                        stats.stopped = true;
                        stats.stop_reason = "conditional branch outside function";
                        stats.stop_address = target;
                        return stats;
                    }
                    break;
                }

                case IROperation::INDIRECT_BRANCH: {
                    uint32_t target = resolve(inst.operands[0]);
                    cpu_->set_thumb((target & 1) != 0);
                    cpu_->r[15] = target & ~1u;
                    stats.stopped = true;
                    stats.stop_reason = "indirect branch";
                    stats.stop_address = target;
                    return stats;
                }

                case IROperation::CALL: {
                    ++stats.bl_calls;
                    if (inst.is_indirect) {
                        uint32_t target = resolve(inst.operands[0]);
                        cpu_->r[14] = inst.address + 4;
                        bool target_thumb = (target & 1) != 0;
                        uint32_t clean_target = target & ~1u;
                        bool prev_thumb = cpu_->thumb;
                        cpu_->set_thumb(target_thumb);
                        if (program_) {
                            auto fit = program_->functions.find(clean_target);
                            if (fit == program_->functions.end()) fit = program_->functions.find(target);
                            if (fit != program_->functions.end()) {
                                cpu_->set_thumb(fit->second.thumb);
                                auto sub_stats = execute_function(fit->first, max_instructions, global_count);
                                stats.instructions_executed += sub_stats.instructions_executed;
                                stats.unknown_encountered += sub_stats.unknown_encountered;
                                stats.unimpl_encountered += sub_stats.unimpl_encountered;
                                stats.arm_executed += sub_stats.arm_executed;
                                stats.thumb_executed += sub_stats.thumb_executed;
                                stats.blocks_executed += sub_stats.blocks_executed;
                                stats.functions_executed += sub_stats.functions_executed;
                                stats.bl_calls += sub_stats.bl_calls;
                                stats.bx_returns += sub_stats.bx_returns;
                                cpu_->set_thumb(prev_thumb);
                                if (sub_stats.stopped && sub_stats.stop_reason != "return") {
                                    stats.stopped = true;
                                    stats.stop_reason = sub_stats.stop_reason;
                                    stats.stop_address = sub_stats.stop_address;
                                    return stats;
                                }
                                break;
                            }
                        }
                        stats.stopped = true;
                        stats.stop_reason = "indirect call";
                        stats.stop_address = target;
                        return stats;
                    }
                    uint32_t target = resolve(inst.operands[0]);
                    cpu_->r[14] = inst.address + 4;
                    bool is_blx = (inst.original_type == arm::InstructionType::BLX);
                    bool prev_thumb = cpu_->thumb;
                    if (is_blx) {
                        if ((target & 1) != 0) cpu_->set_thumb(true);
                        else if ((target & 2) != 0 && is_blx) cpu_->set_thumb(false);
                        else cpu_->set_thumb(!prev_thumb);
                    }
                    if (program_) {
                        uint32_t clean = target & ~1u & ~2u;
                        auto fit = program_->functions.find(target);
                        if (fit == program_->functions.end()) fit = program_->functions.find(clean);
                        if (fit == program_->functions.end()) fit = program_->functions.find(target & ~1u);
                        if (fit != program_->functions.end()) {
                            if (!is_blx) cpu_->set_thumb(fit->second.thumb);
                            else cpu_->set_thumb(fit->second.thumb);
                            uint32_t fn_addr = fit->first;
                            auto sub_stats = execute_function(fn_addr, max_instructions, global_count);
                            stats.instructions_executed += sub_stats.instructions_executed;
                            stats.unknown_encountered += sub_stats.unknown_encountered;
                            stats.unimpl_encountered += sub_stats.unimpl_encountered;
                            stats.arm_executed += sub_stats.arm_executed;
                            stats.thumb_executed += sub_stats.thumb_executed;
                            stats.blocks_executed += sub_stats.blocks_executed;
                            stats.functions_executed += sub_stats.functions_executed;
                            stats.bl_calls += sub_stats.bl_calls;
                            stats.bx_returns += sub_stats.bx_returns;
                            cpu_->set_thumb(prev_thumb);
                            if (sub_stats.stopped && sub_stats.stop_reason != "return") {
                                stats.stopped = true;
                                stats.stop_reason = sub_stats.stop_reason;
                                stats.stop_address = sub_stats.stop_address;
                                return stats;
                            }
                            break;
                        }
                    }
                    cpu_->set_thumb(prev_thumb);
                    stats.stopped = true;
                    stats.stop_reason = "call to unknown function " + std::to_string(target);
                    stats.stop_address = target;
                    return stats;
                }

                case IROperation::RETURN: {
                    ++stats.bx_returns;
                    uint32_t target = resolve(inst.operands[0]);
                    cpu_->set_thumb((target & 1) != 0);
                    cpu_->r[15] = target & ~1u;
                    stats.stop_reason = "return";
                    stats.stop_address = target;
                    stats.stopped = true;
                    return stats;
                }

                case IROperation::MRS: {
                    uint32_t val = 0;
                    if (inst.operands.size() >= 1 && inst.operands[0].kind == IRValueKind::Immediate) {
                        // If operand is 0 → CPSR, 1 → SPSR
                        val = (inst.operands[0].immediate == 0) ? cpu_->cpsr : cpu_->spsr;
                    } else {
                        val = cpu_->cpsr;
                    }
                    write_dest(*inst.dest, val);
                    break;
                }

                case IROperation::MSR: {
                    uint32_t val = resolve(inst.operands[0]);
                    if (inst.operands.size() >= 2 && inst.operands[1].kind == IRValueKind::Immediate) {
                        if (inst.operands[1].immediate & 1) {
                            cpu_->set_flags((val >> 31) & 1, (val >> 30) & 1, (val >> 29) & 1, (val >> 28) & 1);
                        }
                    } else {
                        cpu_->cpsr = val;
                        cpu_->sync_thumb_from_cpsr();
                    }
                    break;
                }

                case IROperation::SWI: {
                    uint32_t id = inst.operands.empty() ? 0 : resolve(inst.operands[0]);
                    runtime::nds_swi(id);
                    // Phase 3.1: SWI is handled by nds_swi layer; do not stop execution
                    // Known boot SWIs (0x00,0x05,0x06,0x0B,0x0C, etc) are stubs that return.
                    // For unknown SWI we still continue but stats will show unknown_swi count.
                    // Keep PC advancing - interpreter will fall through to next block
                    break;
                }

                case IROperation::BKPT: {
                    uint32_t id = inst.operands.empty() ? 0 : resolve(inst.operands[0]);
                    runtime::nds_bkpt(id);
                    // BKPT treated as nop for Phase 3.1, do not stop
                    break;
                }

                case IROperation::MRC: {
                    uint32_t cp_num = resolve(inst.operands[0]);
                    uint32_t crn = resolve(inst.operands[1]);
                    uint32_t crm = resolve(inst.operands[2]);
                    uint32_t op2 = resolve(inst.operands[3]);
                    uint32_t val = runtime::nds_mrc(cp_num, op2, crn, crm);
                    write_dest(*inst.dest, val);
                    break;
                }

                case IROperation::MCR: {
                    uint32_t val = resolve(inst.operands[0]);
                    uint32_t cp_num = resolve(inst.operands[1]);
                    uint32_t crn = resolve(inst.operands[2]);
                    uint32_t crm = resolve(inst.operands[3]);
                    uint32_t op2 = resolve(inst.operands[4]);
                    runtime::nds_mcr(cp_num, op2, val, crn, crm);
                    break;
                }

                case IROperation::UNKNOWN: {
                    ++stats.unknown_encountered;
                    uint32_t raw = inst.raw;
                    runtime::nds_unimplemented(inst.address, raw);
                    if (runtime::has_unimplemented_fault()) {
                        stats.stopped = true;
                        stats.stop_reason = "unimplemented at " + std::to_string(inst.address);
                        stats.stop_address = inst.address;
                        runtime::clear_fault();
                        return stats;
                    }
                    break;
                }

                default: {
                    ++stats.unknown_encountered;
                    stats.stopped = true;
                    stats.stop_reason = "unhandled IR op";
                    stats.stop_address = inst.address;
                    return stats;
                }
            }

            if (do_deep) {
                deep_entry.cpsr = cpu_->cpsr;
                for (int i=0;i<16;++i) deep_entry.r[i]=cpu_->r[i];
                push_deep_trace(deep_entry);
            }

            // If we hit a branch, stop processing the rest of this block
            if (block_branch_taken) break;
        }

        if (stats.stopped) break;

        // Advance to next block
        if (block_branch_taken) {
            current_block_addr = next_block;
        } else {
            // Fall through: find the next block in this function, or search other functions
            bool found_next = false;
            bool found_current = false;
            for (uint32_t addr : fn.blocks) {
                if (addr == current_block_addr) { found_current = true; continue; }
                if (found_current) { current_block_addr = addr; found_next = true; break; }
            }
            if (!found_next) {
                // Search ALL functions for a block at the next sequential address
                if (program_) {
                    // Find the current block's end
                    auto blk_it = current_block_map.find(current_block_addr);
                    if (blk_it != current_block_map.end()) {
                        uint32_t block_end = blk_it->second->end;
                        for (auto& [faddr, ffunc] : program_->functions) {
                            auto fit = ffunc.block_map.find(block_end);
                            if (fit != ffunc.block_map.end()) {
                                current_block_addr = block_end;
                                // Rebuild current_block_map for the new function context
                                current_block_map.clear();
                                for (auto& [addr, blk] : ffunc.block_map) {
                                    current_block_map[addr] = &blk;
                                }
                                // Update fn reference
                                // We can't change the const ref, but we can break and re-enter
                                // For now, just use the new current_block_map
                                found_next = true;
                                break;
                            }
                        }
                    }
                }
if (!found_next) {
                    stats.stopped = true;
                    stats.stop_reason = "fall-through past last block";
                    stats.stop_address = current_block_addr;
                    break;
                }
            }
        }
    }

    if (!stats.stopped && stats.instructions_executed >= max_instructions) {
        stats.stopped = true;
        stats.stop_reason = "instruction limit reached";
    }

    return stats;
}

} // namespace descomp::ir
 
