#include "ir/ir_translator.h"
#include <algorithm>

using namespace descomp::arm;

namespace descomp::ir {

namespace {

IRInstruction make_base(const arm::ARMInstruction& inst) {
    IRInstruction ir;
    ir.address = inst.address;
    ir.raw = inst.raw;
    ir.is_thumb = inst.is_thumb;
    ir.size = inst.is_thumb ? 2 : 4;
    ir.original_type = inst.type;
    ir.mnemonic = inst.mnemonic;
    if (inst.condition != arm::Condition::AL) {
        ir.condition = inst.condition;
    }
    return ir;
}

IRFlagMask flags_for(IROperation op, bool sets, bool is_shift) {
    if (is_shift) {
        // Shifts update N, Z and C (V unaffected / unchanged).
        return sets ? (FLAG_N | FLAG_Z | FLAG_C) : 0;
    }
    switch (op) {
        case IROperation::CMP:
        case IROperation::CMN:
        case IROperation::TST:
        case IROperation::TEQ:
            return FLAG_ALL; // comparisons always affect flags
        case IROperation::MUL:
        case IROperation::UMULL:
        case IROperation::UMLAL:
        case IROperation::SMULL:
        case IROperation::SMLAL:
            return sets ? (FLAG_N | FLAG_Z) : 0;
        case IROperation::ADD:
        case IROperation::ADC:
        case IROperation::SUB:
        case IROperation::SBC:
        case IROperation::RSB:
        case IROperation::RSC:
        case IROperation::AND:
        case IROperation::ORR:
        case IROperation::EOR:
        case IROperation::BIC:
        case IROperation::MOV:
        case IROperation::MVN:
            return sets ? FLAG_ALL : 0;
        default:
            return 0;
    }
}

IROperation map_alu_op(arm::InstructionType t) {
    switch (t) {
        case arm::InstructionType::MOV: return IROperation::MOV;
        case arm::InstructionType::MVN: return IROperation::MVN;
        case arm::InstructionType::ADD: return IROperation::ADD;
        case arm::InstructionType::ADC: return IROperation::ADC;
        case arm::InstructionType::SUB: return IROperation::SUB;
        case arm::InstructionType::SBC: return IROperation::SBC;
        case arm::InstructionType::RSB: return IROperation::RSB;
        case arm::InstructionType::RSC: return IROperation::RSC;
        case arm::InstructionType::AND: return IROperation::AND;
        case arm::InstructionType::ORR: return IROperation::ORR;
        case arm::InstructionType::EOR: return IROperation::EOR;
        case arm::InstructionType::BIC: return IROperation::BIC;
        case arm::InstructionType::CMP: return IROperation::CMP;
        case arm::InstructionType::CMN: return IROperation::CMN;
        case arm::InstructionType::TST: return IROperation::TST;
        case arm::InstructionType::TEQ: return IROperation::TEQ;
        case arm::InstructionType::MUL: return IROperation::MUL;
        case arm::InstructionType::LSL: return IROperation::LSL;
        case arm::InstructionType::LSR: return IROperation::LSR;
        case arm::InstructionType::ASR: return IROperation::ASR;
        case arm::InstructionType::ROR: return IROperation::ROR;
        default: return IROperation::UNKNOWN;
    }
}

IRMemoryAccess access_for(arm::InstructionType t) {
    switch (t) {
        case arm::InstructionType::LDRB:
        case arm::InstructionType::STRB:
        case arm::InstructionType::LDRSB:
            return IRMemoryAccess::Byte;
        case arm::InstructionType::LDRH:
        case arm::InstructionType::STRH:
        case arm::InstructionType::LDRSH:
            return IRMemoryAccess::HalfWord;
        default:
            return IRMemoryAccess::Word;
    }
}

bool is_sign_extend_load(arm::InstructionType t) {
    return t == arm::InstructionType::LDRSB || t == arm::InstructionType::LDRSH;
}

void set_control_flow(IRInstruction& ir, bool terminator, bool indirect, bool call = false) {
    ir.is_control_flow = true;
    ir.is_terminator = terminator;
    ir.is_indirect = indirect;
    ir.is_call = call;
}

} // namespace

IRValue IRTranslator::build_shift_operand(
    const arm::ARMInstruction& inst,
    std::vector<IRInstruction>& out,
    uint32_t& temp_counter)
{
    IROperation shift_op = map_alu_op(inst.shift_type == arm::ShiftType::LSL ? arm::InstructionType::LSL
                                : inst.shift_type == arm::ShiftType::LSR ? arm::InstructionType::LSR
                                : inst.shift_type == arm::ShiftType::ASR ? arm::InstructionType::ASR
                                : arm::InstructionType::ROR);

    IRValue temp = IRValue::temp(temp_counter++);
    IRInstruction shift_ir;
    shift_ir.op = shift_op;
    shift_ir.dest = temp;
    shift_ir.address = inst.address;
    shift_ir.is_thumb = inst.is_thumb;
    shift_ir.size = inst.is_thumb ? 2 : 4;
    shift_ir.mnemonic = "SHIFT";
    shift_ir.condition = (inst.condition != arm::Condition::AL) ? std::optional<arm::Condition>(inst.condition) : std::nullopt;

    if (inst.has_reg_shift) {
        // Rm <shift> Rs
        shift_ir.operands = { IRValue::reg(inst.rm), IRValue::reg(inst.shift_reg) };
    } else {
        // Rm <shift> #imm
        shift_ir.operands = { IRValue::reg(inst.rm), IRValue::imm(inst.shift_amount) };
    }
    out.push_back(std::move(shift_ir));
    return temp;
}

IRMemory IRTranslator::build_memory_operand(const arm::ARMInstruction& inst) {
    IRMemory m;
    m.base_reg = inst.rn;
    m.access = access_for(inst.type);
    m.sign_extend = is_sign_extend_load(inst.type);
    m.writeback = inst.writeback;
    m.pre_indexed = inst.pre_indexed;

    // For ARM single/halfword transfer the decoder distinguishes register vs
    // immediate offset via rm (REG_NONE when immediate).
    bool reg_offset = (inst.rm != REG_NONE);

    if (reg_offset) {
        m.index_reg = inst.rm;
        m.index_shift = inst.shift_type;
        m.index_shift_amount = inst.shift_amount;
        m.add_index = inst.add_offset;
        m.displacement = 0;
    } else {
        m.displacement = inst.immediate; // already signed in the decoder
    }
    return m;
}

std::vector<IRInstruction> IRTranslator::lift(const arm::ARMInstruction& inst) {
    std::vector<IRInstruction> out;
    uint32_t temp_counter = 0;

    switch (inst.type) {
        case arm::InstructionType::MOV:
        case arm::InstructionType::MVN:
        case arm::InstructionType::ADD:
        case arm::InstructionType::ADC:
        case arm::InstructionType::SUB:
        case arm::InstructionType::SBC:
        case arm::InstructionType::RSB:
        case arm::InstructionType::RSC:
        case arm::InstructionType::AND:
        case arm::InstructionType::ORR:
        case arm::InstructionType::EOR:
        case arm::InstructionType::BIC: {
            IROperation op = map_alu_op(inst.type);
            IRInstruction ir = make_base(inst);
            ir.op = op;
            ir.update_flags = inst.sets_flags;
            ir.affected_flags = flags_for(op, inst.sets_flags, false);

            // Resolve operand2 (register/immediate, possibly shifted).
            IRValue op2;
            if (inst.has_immediate) {
                op2 = IRValue::imm(inst.immediate);
            } else {
                if (inst.has_shift || inst.has_reg_shift) {
                    op2 = build_shift_operand(inst, out, temp_counter);
                } else {
                    op2 = IRValue::reg(inst.rm);
                }
            }

            // Destination register (special handling when writing PC).
            if (inst.rd == Register::PC) {
                if (inst.is_return) {
                    ir.op = IROperation::RETURN;
                    ir.dest = std::nullopt;
                    ir.operands = { IRValue::reg(Register::LR) };
                    ir.update_flags = false;
                    ir.affected_flags = 0;
                    set_control_flow(ir, true, false);
                } else {
                    // ADD/MOV PC, Rm  -> indirect branch to computed value.
                    if (op == IROperation::MOV) {
                        ir.operands = { op2 };
                    } else {
                        ir.dest = IRValue::reg(Register::PC);
                        ir.operands = { IRValue::reg(inst.rn), op2 };
                    }
                    ir.is_indirect = true;
                    set_control_flow(ir, true, true);
                }
                out.push_back(std::move(ir));
                break;
            }

            ir.dest = IRValue::reg(inst.rd);

            if (op == IROperation::MOV || op == IROperation::MVN) {
                ir.operands = { op2 };
            } else if (op == IROperation::RSB || op == IROperation::RSC) {
                // Rd = Op2 - Rn
                if (inst.is_thumb && inst.rn == REG_NONE) {
                    // Thumb NEG Rd, Rm  =>  Rd = 0 - Rm
                    ir.operands = { IRValue::imm(0), op2 };
                } else {
                    ir.operands = { op2, IRValue::reg(inst.rn) };
                }
            } else if (inst.is_thumb && inst.rn == REG_NONE) {
                // Thumb format-4 ALU: Rd = Rd OP Rs  (first source is the dest reg)
                ir.operands = { IRValue::reg(inst.rd), op2 };
            } else {
                ir.operands = { IRValue::reg(inst.rn), op2 };
            }
            out.push_back(std::move(ir));
            break;
        }

        case arm::InstructionType::CMP:
        case arm::InstructionType::CMN:
        case arm::InstructionType::TST:
        case arm::InstructionType::TEQ: {
            IROperation op = map_alu_op(inst.type);
            IRInstruction ir = make_base(inst);
            ir.op = op;
            ir.dest = std::nullopt;
            ir.update_flags = true;
            ir.affected_flags = FLAG_ALL;

            IRValue op2;
            if (inst.has_immediate) {
                op2 = IRValue::imm(inst.immediate);
            } else {
                if (inst.has_shift || inst.has_reg_shift) {
                    op2 = build_shift_operand(inst, out, temp_counter);
                } else {
                    op2 = IRValue::reg(inst.rm);
                }
            }
            if (inst.is_thumb && inst.rn == REG_NONE) {
                // Thumb format-4: TST/CMP/CMN/TEQ Rd, Rs  (first source is Rd)
                ir.operands = { IRValue::reg(inst.rd), op2 };
            } else {
                ir.operands = { IRValue::reg(inst.rn), op2 };
            }
            out.push_back(std::move(ir));
            break;
        }
        case arm::InstructionType::MUL:
        case arm::InstructionType::MLA: {
            IROperation op = IROperation::MUL;
            IRInstruction ir = make_base(inst);
            ir.op = op;
            ir.update_flags = inst.sets_flags;
            ir.affected_flags = flags_for(op, inst.sets_flags, false);
            ir.dest = IRValue::reg(inst.rd);
            if (inst.type == arm::InstructionType::MLA) {
                ir.operands = { IRValue::reg(inst.rm), IRValue::reg(inst.rs), IRValue::reg(inst.rn) };
            } else {
                ir.operands = { IRValue::reg(inst.rm), IRValue::reg(inst.rs) };
            }
            out.push_back(std::move(ir));
            break;
        }

        case arm::InstructionType::UMULL:
        case arm::InstructionType::UMLAL:
        case arm::InstructionType::SMULL:
        case arm::InstructionType::SMLAL: {
            IROperation op;
            switch (inst.type) {
                case arm::InstructionType::UMULL: op = IROperation::UMULL; break;
                case arm::InstructionType::UMLAL: op = IROperation::UMLAL; break;
                case arm::InstructionType::SMULL: op = IROperation::SMULL; break;
                case arm::InstructionType::SMLAL: op = IROperation::SMLAL; break;
                default: op = IROperation::UMULL; break;
            }
            IRInstruction ir = make_base(inst);
            ir.op = op;
            ir.update_flags = inst.sets_flags;
            ir.affected_flags = flags_for(op, inst.sets_flags, false);
            ir.dest = IRValue::reg(inst.rn); // RdLo
            ir.dest2 = IRValue::reg(inst.rd); // RdHi
            ir.operands = { IRValue::reg(inst.rm), IRValue::reg(inst.rs) };
            out.push_back(std::move(ir));
            break;
        }

        case arm::InstructionType::LSL:
        case arm::InstructionType::LSR:
        case arm::InstructionType::ASR:
        case arm::InstructionType::ROR: {
            // Thumb shifts. Format1: Rd = Rs <shift> #imm (value = rm, amount = imm).
            // Format4:   Rd = Rd <shift> Rs (value = rd, amount = rm).
            IROperation op = map_alu_op(inst.type);
            IRInstruction ir = make_base(inst);
            ir.op = op;
            ir.update_flags = inst.sets_flags;
            ir.affected_flags = flags_for(op, inst.sets_flags, true);
            ir.dest = IRValue::reg(inst.rd);

            IRValue value;
            IRValue amount;
            if (inst.has_reg_shift) {
                // Register amount: value = Rd, amount = Rs (rm holds Rs)
                value = IRValue::reg(inst.rd);
                amount = IRValue::reg(inst.rm);
            } else if (inst.has_shift) {
                // Immediate amount: value = Rs (rm), amount = imm
                value = IRValue::reg(inst.rm);
                amount = IRValue::imm(inst.shift_amount);
            } else if (inst.shift_amount != 0) {
                // Fallback for ARM immediate shift where has_shift not set but amount present
                value = IRValue::reg(inst.rm);
                amount = IRValue::imm(inst.shift_amount);
            } else {
                value = IRValue::reg(inst.rd);
                amount = IRValue::reg(inst.rm);
            }
            ir.operands = { value, amount };
            out.push_back(std::move(ir));
            break;
        }

        case arm::InstructionType::LDR:
        case arm::InstructionType::STR:
        case arm::InstructionType::LDRB:
        case arm::InstructionType::STRB:
        case arm::InstructionType::LDRH:
        case arm::InstructionType::STRH:
        case arm::InstructionType::LDRSB:
        case arm::InstructionType::LDRSH: {
            IRMemory mem = build_memory_operand(inst);
            IRInstruction ir = make_base(inst);
            bool is_load = (inst.type == arm::InstructionType::LDR ||
                            inst.type == arm::InstructionType::LDRB ||
                            inst.type == arm::InstructionType::LDRH ||
                            inst.type == arm::InstructionType::LDRSB ||
                            inst.type == arm::InstructionType::LDRSH);
            if (is_load) {
                ir.op = IROperation::LOAD;
                ir.dest = IRValue::reg(inst.rd);
                ir.operands = { IRValue::mem(mem) };
            } else {
                ir.op = IROperation::STORE;
                ir.dest = IRValue::mem(mem);
                ir.operands = { IRValue::reg(inst.rd) };
            }

            if (inst.rd == Register::PC && is_load) {
                // LDR PC, [...]  -> indirect control transfer.
                ir.is_indirect = true;
                set_control_flow(ir, true, true);
            }
            out.push_back(std::move(ir));
            break;
        }

        case arm::InstructionType::B: {
            IRInstruction ir = make_base(inst);
            if (inst.is_conditional_branch) {
                ir.op = IROperation::COND_BRANCH;
                ir.condition = inst.condition;
                set_control_flow(ir, true, false);
            } else {
                ir.op = IROperation::BRANCH;
                set_control_flow(ir, true, false);
            }
            ir.operands = { IRValue::imm(static_cast<int64_t>(inst.branch_target)) };
            out.push_back(std::move(ir));
            break;
        }

        case arm::InstructionType::BL: {
            IRInstruction ir = make_base(inst);
            ir.op = IROperation::CALL;
            set_control_flow(ir, false, false, true);
            ir.operands = { IRValue::imm(static_cast<int64_t>(inst.branch_target)) };
            out.push_back(std::move(ir));
            break;
        }

        case arm::InstructionType::BLX: {
            IRInstruction ir = make_base(inst);
            ir.op = IROperation::CALL;
            set_control_flow(ir, false, inst.is_indirect_branch, true);
            if (inst.is_indirect_branch) {
                ir.operands = { IRValue::reg(inst.rm) };
            } else {
                ir.operands = { IRValue::imm(static_cast<int64_t>(inst.branch_target)) };
            }
            out.push_back(std::move(ir));
            break;
        }

        case arm::InstructionType::BX: {
            IRInstruction ir = make_base(inst);
            if (inst.is_return) {
                ir.op = IROperation::RETURN;
                ir.operands = { IRValue::reg(inst.rm) };
                set_control_flow(ir, true, false);
            } else {
                ir.op = IROperation::INDIRECT_BRANCH;
                ir.operands = { IRValue::reg(inst.rm) };
                set_control_flow(ir, true, true);
            }
            out.push_back(std::move(ir));
            break;
        }

        case arm::InstructionType::POP:
        case arm::InstructionType::LDM:
        case arm::InstructionType::PUSH:
        case arm::InstructionType::STM: {
            // Decompose block transfer into per-register loads/stores.
            std::vector<uint8_t> regs;
            for (int i = 0; i < 16; ++i) if (inst.register_list & (1u << i)) regs.push_back(static_cast<uint8_t>(i));
            if (regs.empty()) {
                // Empty register list is UNPREDICTABLE on ARM; treat as NOP for recompilation
                IRInstruction ir = make_base(inst);
                ir.op = IROperation::NOP;
                out.push_back(std::move(ir));
                break;
            }
            bool is_load = (inst.type == arm::InstructionType::LDM || inst.type == arm::InstructionType::POP);
            // Determine addressing mode
            bool pre, add, wb;
            uint8_t base_reg = inst.rn;
            if (inst.is_thumb) {
                if (inst.type == arm::InstructionType::PUSH) { pre = true; add = false; wb = true; base_reg = arm::SP; }
                else if (inst.type == arm::InstructionType::POP) { pre = false; add = true; wb = true; base_reg = arm::SP; }
                else { // LDM/STM thumb always IA
                    pre = false; add = true; wb = true;
                }
            } else {
                pre = inst.pre_indexed;
                add = inst.add_offset;
                wb = inst.writeback;
            }
            int n = (int)regs.size();
            int delta = add ? n*4 : -n*4;
            int start_offset;
            if (add) {
                if (pre) start_offset = 4;
                else start_offset = 0;
            } else {
                if (pre) start_offset = -n*4;
                else start_offset = -n*4 + 4;
            }
            bool base_in_list = false;
            for (auto r : regs) if (r == base_reg) base_in_list = true;
            // Separate PC reg (if load return) for ordering
            std::vector<uint8_t> non_pc_regs;
            bool has_pc = false;
            for (auto r : regs) {
                if (r == arm::PC && is_load) has_pc = true;
                else non_pc_regs.push_back(r);
            }
            auto make_mem = [&](int offset) -> ir::IRMemory {
                ir::IRMemory m;
                m.base_reg = base_reg;
                m.displacement = offset;
                m.index_reg = arm::REG_NONE;
                m.access = ir::IRMemoryAccess::Word;
                m.pre_indexed = true;
                m.writeback = false;
                return m;
            };
            // Emit non-PC transfers
            for (size_t idx = 0; idx < non_pc_regs.size(); ++idx) {
                uint8_t reg = non_pc_regs[idx];
                // Find position of this reg in sorted order (regs is sorted)
                // Need offset index = position in regs (sorted) 0..n-1
                int pos = -1;
                for (int j = 0; j < n; ++j) if (regs[j] == reg) { pos = j; break; }
                int offset = start_offset + pos*4;
                IRInstruction ir = make_base(inst);
                ir.mnemonic = inst.mnemonic;
                if (is_load) {
                    ir.op = ir::IROperation::LOAD;
                    ir.dest = ir::IRValue::reg(reg);
                    ir.operands = { ir::IRValue::mem(make_mem(offset)) };
                } else {
                    ir.op = ir::IROperation::STORE;
                    ir.dest = ir::IRValue::mem(make_mem(offset));
                    ir.operands = { ir::IRValue::reg(reg) };
                }
                out.push_back(std::move(ir));
            }
            // Writeback update (before PC if return)
            if (wb && !base_in_list) {
                // For return case, update before PC load to keep reachable
                IRInstruction wb_ir = make_base(inst);
                wb_ir.mnemonic = inst.mnemonic;
                wb_ir.mnemonic += " wb";
                if (delta > 0) {
                    wb_ir.op = ir::IROperation::ADD;
                    wb_ir.dest = ir::IRValue::reg(base_reg);
                    wb_ir.operands = { ir::IRValue::reg(base_reg), ir::IRValue::imm(delta) };
                } else {
                    wb_ir.op = ir::IROperation::SUB;
                    wb_ir.dest = ir::IRValue::reg(base_reg);
                    wb_ir.operands = { ir::IRValue::reg(base_reg), ir::IRValue::imm(-delta) };
                }
                wb_ir.update_flags = false;
                wb_ir.affected_flags = 0;
                out.push_back(std::move(wb_ir));
            }
            // PC load (if present)
            if (has_pc) {
                int pos = -1;
                for (int j = 0; j < n; ++j) if (regs[j] == arm::PC) { pos = j; break; }
                int offset = start_offset + pos*4;
                IRInstruction ir = make_base(inst);
                ir.mnemonic = inst.mnemonic;
                ir.op = ir::IROperation::LOAD;
                ir.dest = ir::IRValue::reg(arm::PC);
                ir.operands = { ir::IRValue::mem(make_mem(offset)) };
                ir.is_control_flow = true;
                ir.is_terminator = true;
                ir.is_indirect = false;
                // Also mark as return for emitter convenience
                // Keep is_return semantics via being LOAD to PC + terminator
                out.push_back(std::move(ir));
                // For POP/LDM that is_return, the final IR should also be a RETURN.
                // Emit an explicit RETURN after the PC load so emitter produces `return;`.
                IRInstruction ret = make_base(inst);
                ret.op = ir::IROperation::RETURN;
                ret.mnemonic = "RETURN";
                // No operands needed, but keep provenance
                ret.is_control_flow = true;
                ret.is_terminator = true;
                out.push_back(std::move(ret));
            }
            break;
        }

        case arm::InstructionType::NOP: {
            IRInstruction ir = make_base(inst);
            ir.op = IROperation::NOP;
            out.push_back(std::move(ir));
            break;
        }

        case arm::InstructionType::CLZ: {
            IRInstruction ir = make_base(inst);
            ir.op = IROperation::CLZ;
            ir.dest = IRValue::reg(inst.rd);
            ir.operands = { IRValue::reg(inst.rm) };
            ir.update_flags = false;
            out.push_back(std::move(ir));
            break;
        }

        case arm::InstructionType::MRS: {
            IRInstruction ir = make_base(inst);
            ir.op = IROperation::MRS;
            ir.dest = IRValue::reg(inst.rd);
            ir.spsr = inst.spsr;
            ir.update_flags = false;
            out.push_back(std::move(ir));
            break;
        }

        case arm::InstructionType::MSR: {
            IRInstruction ir = make_base(inst);
            ir.op = IROperation::MSR;
            ir.spsr = inst.spsr;
            ir.msr_mask = inst.msr_mask;
            if (inst.has_immediate) ir.operands = { IRValue::imm(inst.immediate) };
            else ir.operands = { IRValue::reg(inst.rm) };
            ir.update_flags = false;
            out.push_back(std::move(ir));
            break;
        }

        case arm::InstructionType::SWI: {
            IRInstruction ir = make_base(inst);
            ir.op = IROperation::SWI;
            ir.operands = { IRValue::imm(inst.immediate) };
            ir.is_control_flow = false;
            out.push_back(std::move(ir));
            break;
        }

        case arm::InstructionType::BKPT: {
            IRInstruction ir = make_base(inst);
            ir.op = IROperation::BKPT;
            ir.operands = { IRValue::imm(inst.immediate) };
            out.push_back(std::move(ir));
            break;
        }

        case arm::InstructionType::MRC: {
            IRInstruction ir = make_base(inst);
            ir.op = IROperation::MRC;
            ir.dest = IRValue::reg(inst.rd);
            // Encode coprocessor info in operands: [cp_num, crn, crm, op2]
            ir.operands = { IRValue::imm(inst.cp_num), IRValue::imm(inst.crn),
                            IRValue::imm(inst.crm), IRValue::imm(inst.cp_op2) };
            out.push_back(std::move(ir));
            break;
        }

        case arm::InstructionType::MCR: {
            IRInstruction ir = make_base(inst);
            ir.op = IROperation::MCR;
            ir.operands = { IRValue::reg(inst.rd),
                            IRValue::imm(inst.cp_num), IRValue::imm(inst.crn),
                            IRValue::imm(inst.crm), IRValue::imm(inst.cp_op2) };
            out.push_back(std::move(ir));
            break;
        }

        default: {
            // Anything not explicitly lowered yet: keep provenance, mark UNKNOWN.
            IRInstruction ir = make_base(inst);
            ir.op = IROperation::UNKNOWN;
            if (inst.rd != REG_NONE) ir.dest = IRValue::reg(inst.rd);
            if (inst.rn != REG_NONE && inst.type != arm::InstructionType::UNKNOWN) {
                ir.operands.push_back(IRValue::reg(inst.rn));
            }
            if (inst.rm != REG_NONE) ir.operands.push_back(IRValue::reg(inst.rm));
            if (inst.has_immediate) ir.operands.push_back(IRValue::imm(inst.immediate));
            for (int i = 0; i < 16; ++i) {
                if (inst.register_list & (1u << i)) ir.register_list.push_back(static_cast<uint8_t>(i));
            }
            out.push_back(std::move(ir));
            break;
        }
    }

    return out;
}

IRBasicBlock IRTranslator::lift_block(const analysis::BasicBlock& bb) {
    IRBasicBlock ir_bb;
    ir_bb.start = bb.start;
    ir_bb.end = bb.end;
    ir_bb.is_thumb = false;
    ir_bb.is_entry = bb.is_entry;
    ir_bb.is_exit = bb.is_exit;
    ir_bb.has_indirect_jump = bb.has_indirect_jump;
    ir_bb.has_unknown_target = bb.has_unknown_target;
    ir_bb.successors = bb.successors;
    ir_bb.predecessors = bb.predecessors;

    for (const auto& inst : bb.instructions) {
        auto lifted = lift(inst);
        for (auto& li : lifted) {
            ir_bb.is_thumb = ir_bb.is_thumb || li.is_thumb;
            ir_bb.instructions.push_back(std::move(li));
        }
    }
    return ir_bb;
}

IRFunction IRTranslator::lift_function(
    const analysis::Function& fn,
    const std::map<uint32_t, analysis::BasicBlock>& blocks)
{
    IRFunction ir_fn;
    ir_fn.address = fn.address;
    ir_fn.name = fn.name;
    ir_fn.thumb = fn.thumb;
    ir_fn.size = fn.size;
    ir_fn.callees = fn.callees;
    ir_fn.callers = fn.callers;
    ir_fn.indirect_branches = fn.indirect_branches;
    ir_fn.indirect_calls = fn.indirect_calls;
    ir_fn.unknown_targets = fn.unknown_targets;

    for (uint32_t blk_start : fn.blocks) {
        auto it = blocks.find(blk_start);
        if (it == blocks.end()) continue;
        IRBasicBlock ir_bb = lift_block(it->second);
        ir_fn.block_map[blk_start] = std::move(ir_bb);
        ir_fn.blocks.push_back(blk_start);
    }
    return ir_fn;
}

IRProgram IRTranslator::lift_program(
    const std::map<uint32_t, analysis::BasicBlock>& blocks,
    const std::map<uint32_t, analysis::Function>& functions)
{
    IRProgram program;
    for (const auto& [addr, fn] : functions) {
        program.functions[addr] = lift_function(fn, blocks);
    }
    return program;
}

} // namespace descomp::ir
