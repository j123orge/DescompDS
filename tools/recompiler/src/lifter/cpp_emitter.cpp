#include "lifter/cpp_emitter.h"
#include "ir/ir_value.h"

#include <iomanip>
#include <sstream>
#include <set>

namespace descomp::lifter {

namespace {

std::string hex8(uint32_t v) {
    std::ostringstream ss;
    ss << "0x" << std::hex << std::uppercase << std::setw(8) << std::setfill('0') << v;
    return ss.str();
}

std::string hex_raw(uint32_t raw, bool is_thumb) {
    std::ostringstream ss;
    ss << "0x" << std::hex << std::uppercase << std::setw(is_thumb ? 4 : 8) << std::setfill('0') << (raw & (is_thumb ? 0xFFFF : 0xFFFFFFFF));
    return ss.str();
}

// Condition to C++ boolean expression over flag variables.
// flag variables in generated code are named flag_n, flag_z, flag_c, flag_v.
std::string cond_to_cpp(arm::Condition c) {
    switch (c) {
        case arm::Condition::EQ: return "(flag_z)";
        case arm::Condition::NE: return "(!flag_z)";
        case arm::Condition::CS: return "(flag_c)"; // HS alias (same value)
        case arm::Condition::CC: return "(!flag_c)"; // LO alias
        case arm::Condition::MI: return "(flag_n)";
        case arm::Condition::PL: return "(!flag_n)";
        case arm::Condition::VS: return "(flag_v)";
        case arm::Condition::VC: return "(!flag_v)";
        case arm::Condition::HI: return "(flag_c && !flag_z)";
        case arm::Condition::LS: return "(!flag_c || flag_z)";
        case arm::Condition::GE: return "(flag_n == flag_v)";
        case arm::Condition::LT: return "(flag_n != flag_v)";
        case arm::Condition::GT: return "(!flag_z && flag_n == flag_v)";
        case arm::Condition::LE: return "(flag_z || flag_n != flag_v)";
        case arm::Condition::AL: return "(true)";
        case arm::Condition::NV: return "(false)";
    }
    return "(true)";
}

inline bool uses_reg(const ir::IRInstruction& inst, uint8_t reg) {
    if (inst.dest && inst.dest->kind == ir::IRValueKind::Register && inst.dest->id == reg) return true;
    if (inst.dest2 && inst.dest2->kind == ir::IRValueKind::Register && inst.dest2->id == reg) return true;
    for (auto &op : inst.operands) {
        if (op.kind == ir::IRValueKind::Register && op.id == reg) return true;
        if (op.kind == ir::IRValueKind::Memory && (op.memory.base_reg == reg || op.memory.index_reg == reg)) return true;
    }
    return false;
}

} // namespace

std::string CppEmitter::reg_expr(uint8_t reg) {
    return "cpu.r[" + std::to_string(reg) + "]";
}

std::string CppEmitter::imm_expr(int64_t imm) {
    if (imm < 0) return std::to_string(imm);
    std::ostringstream ss;
    ss << imm << "u";
    // also show hex in comment via provenance, value itself is decimal
    return ss.str();
}

std::string CppEmitter::flag_expr(ir::IRFlag f) {
    switch (f) {
        case ir::IRFlag::N: return "flag_n";
        case ir::IRFlag::Z: return "flag_z";
        case ir::IRFlag::C: return "flag_c";
        case ir::IRFlag::V: return "flag_v";
    }
    return "flag_unknown";
}

std::string CppEmitter::temp_expr(uint32_t id) {
    return "t" + std::to_string(id);
}

std::string CppEmitter::value_expr(const ir::IRValue& v) {
    switch (v.kind) {
        case ir::IRValueKind::Register: return reg_expr(static_cast<uint8_t>(v.id));
        case ir::IRValueKind::Immediate: return imm_expr(v.immediate);
        case ir::IRValueKind::Temporary: return temp_expr(v.id);
        case ir::IRValueKind::Flag: return flag_expr(static_cast<ir::IRFlag>(v.id));
        case ir::IRValueKind::Memory: return "/*mem*/0";
    }
    return "0";
}

std::string CppEmitter::mem_addr_expr(const ir::IRMemory& m) {
    // Build a C++ expression that computes the effective address.
    // For testing we expose the structure; the string is used inside nds_hal_* calls.
    std::ostringstream ss;
    ss << reg_expr(m.base_reg);
    if (m.index_reg != arm::REG_NONE) {
        std::string idx = reg_expr(m.index_reg);
        if (m.index_shift_amount != 0) {
            std::string shift_op;
            switch (m.index_shift) {
                case arm::ShiftType::LSL: shift_op = " << "; break;
                case arm::ShiftType::LSR: shift_op = " >> "; break;
                case arm::ShiftType::ASR: shift_op = " >> "; break; // arithmetic handled via helper
                case arm::ShiftType::ROR: shift_op = " /*ROR*/ >> "; break;
                default: shift_op = " << "; break;
            }
            idx = "(" + idx + shift_op + std::to_string(m.index_shift_amount) + ")";
        }
        ss << (m.add_index ? " + " : " - ") << idx;
    } else if (m.displacement != 0) {
        if (m.displacement > 0) ss << " + " << m.displacement;
        else ss << " - " << (-m.displacement);
    }
    return ss.str();
}

std::string CppEmitter::condition_expr(arm::Condition cond) {
    return cond_to_cpp(cond);
}

std::string CppEmitter::indent_str() const {
    return std::string(m_indent * 4, ' ');
}

void CppEmitter::emit_line(const std::string& line) {
    m_out << indent_str() << line << "\n";
}

void CppEmitter::emit_raw(const std::string& text) {
    m_out << text;
}

void CppEmitter::clear() {
    m_out.str("");
    m_out.clear();
    m_indent = 0;
}

void CppEmitter::emit_prelude() {
    emit_raw("#pragma once\n");
    emit_raw("#include <cstdint>\n");
    emit_raw("#include <cstdio>\n");
    emit_raw("#include \"runtime/nds_runtime.h\"\n");
    emit_raw("\n");
    emit_raw("// DescompDS generated file — uses nds_runtime HAL\n");
    emit_raw("using descomp::runtime::CPUState;\n");
    emit_raw("static inline uint32_t nds_hal_read32(uint32_t addr) { return descomp::runtime::read32(addr); }\n");
    emit_raw("static inline uint16_t nds_hal_read16(uint32_t addr) { return descomp::runtime::read16(addr); }\n");
    emit_raw("static inline uint8_t  nds_hal_read8(uint32_t addr)  { return descomp::runtime::read8(addr); }\n");
    emit_raw("static inline void nds_hal_write32(uint32_t addr, uint32_t v) { descomp::runtime::write32(addr, v); }\n");
    emit_raw("static inline void nds_hal_write16(uint32_t addr, uint16_t v) { descomp::runtime::write16(addr, v); }\n");
    emit_raw("static inline void nds_hal_write8(uint32_t addr, uint8_t v)   { descomp::runtime::write8(addr, v); }\n");
    emit_raw("static inline void nds_unimplemented(uint32_t addr, uint32_t raw) { descomp::runtime::nds_unimplemented(addr, raw); }\n");
    emit_raw("static inline void nds_swi(uint32_t id) { descomp::runtime::nds_swi(id); }\n");
    emit_raw("static inline void nds_bkpt(uint32_t id) { descomp::runtime::nds_bkpt(id); }\n");
    emit_raw("static inline uint32_t nds_lsl(uint32_t v, uint32_t s) { return s >= 32 ? 0 : v << s; }\n");
    emit_raw("static inline uint32_t nds_lsr(uint32_t v, uint32_t s) { return s >= 32 ? 0 : v >> s; }\n");
    emit_raw("static inline uint32_t nds_asr(uint32_t v, uint32_t s) { return s >= 32 ? (v>>31 ? 0xFFFFFFFFu : 0) : (uint32_t)((int32_t)v >> s); }\n");
    emit_raw("static inline uint32_t nds_ror(uint32_t v, uint32_t s) { s &= 31; return s==0? v : (v >> s) | (v << (32-s)); }\n");
    emit_raw("static inline uint32_t nds_clz(uint32_t v) { if(v==0) return 32; uint32_t n=0; while((v>>31)==0){n++; v<<=1;} return n; }\n");
    emit_raw("static inline uint32_t nds_mrc(uint32_t cp, uint32_t op2, uint32_t crn, uint32_t crm) { (void)cp;(void)op2;(void)crn;(void)crm; return 0; }\n");
    emit_raw("static inline void nds_mcr(uint32_t cp, uint32_t op2, uint32_t val, uint32_t crn, uint32_t crm) { (void)cp;(void)op2;(void)val;(void)crn;(void)crm; }\n");
    emit_raw("\n");
}

void CppEmitter::emit_condition_wrapper(const std::optional<arm::Condition>& cond,
                                        const ir::IRInstruction& inst,
                                        const std::string& body_indent_extra) {
    (void)inst;
    (void)body_indent_extra;
    // This helper is not used directly; per-instruction emitters handle wrapping.
    (void)cond;
}

void CppEmitter::emit_flags_update(const ir::IRInstruction& inst, const std::string& result_var) {
    if (!inst.update_flags) return;
    // Emit N/Z always from result, C/V where applicable.
    // For CMP/CMN/TST/TEQ the result_var is a temporary holding the flag computation result.
    // We emit precise inline logic for each class.
    if (inst.op == ir::IROperation::CMP || inst.op == ir::IROperation::CMN ||
        inst.op == ir::IROperation::TST || inst.op == ir::IROperation::TEQ ||
        inst.op == ir::IROperation::SUB || inst.op == ir::IROperation::RSB ||
        inst.op == ir::IROperation::SBC || inst.op == ir::IROperation::RSC ||
        inst.op == ir::IROperation::ADD || inst.op == ir::IROperation::ADC ||
        inst.op == ir::IROperation::AND || inst.op == ir::IROperation::ORR ||
        inst.op == ir::IROperation::EOR || inst.op == ir::IROperation::BIC ||
        inst.op == ir::IROperation::MOV || inst.op == ir::IROperation::MVN ||
        inst.op == ir::IROperation::LSL || inst.op == ir::IROperation::LSR ||
        inst.op == ir::IROperation::ASR || inst.op == ir::IROperation::ROR ||
        inst.op == ir::IROperation::MUL) {
        emit_line("flag_n = (" + result_var + " >> 31) & 1;");
        emit_line("flag_z = (" + result_var + " == 0);");
        if (inst.affected_flags & ir::FLAG_C) {
            // Approximate: for ADD, C = carry out; for SUB/CMP, C = not borrow.
            // We emit generic conservation; precise C logic is inlined per op below, this is fallback.
            // Actual per-op C is emitted in the op-specific emitter before calling this.
        }
        if (inst.affected_flags & ir::FLAG_V) {
            // V handled per op.
        }
    }
}

// ---------------------------------------------------------------------------
// Per-opcode emitters
// ---------------------------------------------------------------------------

void CppEmitter::emit_mov(const ir::IRInstruction& inst) {
    std::string dst = value_expr(*inst.dest);
    std::string src = value_expr(inst.operands[0]);
    emit_line(dst + " = " + src + ";");
    if (inst.update_flags) {
        emit_line("flag_n = (" + dst + " >> 31) & 1;");
        emit_line("flag_z = (" + dst + " == 0);");
        if (inst.affected_flags & ir::FLAG_C) emit_line("// flag_c preserved/updated by shifter (approx)");
        if (inst.affected_flags & ir::FLAG_V) emit_line("// flag_v unchanged for MOV");
    }
}

void CppEmitter::emit_arith(const ir::IRInstruction& inst) {
    std::string dst = value_expr(*inst.dest);
    std::string a = value_expr(inst.operands[0]);
    std::string b = value_expr(inst.operands[1]);

    if (inst.op == ir::IROperation::ADD) {
        emit_line(dst + " = " + a + " + " + b + ";");
        if (inst.update_flags) {
            emit_line("{ uint32_t _a=" + a + ", _b=" + b + ", _r=" + dst + "; flag_n=(_r>>31)&1; flag_z=(_r==0); flag_c=((uint64_t)_a + _b)>>32; flag_v=((~(_a ^ _b) & (_a ^ _r))>>31)&1; }");
        }
    } else if (inst.op == ir::IROperation::ADC) {
        emit_line("{ bool _c_in=flag_c; uint64_t _u=(uint64_t)" + a + " + " + b + " + (_c_in?1:0); int64_t _s=(int64_t)(int32_t)" + a + " + (int64_t)(int32_t)" + b + " + (_c_in?1:0); " + dst + "=(uint32_t)_u;");
        if (inst.update_flags) emit_line("  flag_n=(" + dst + ">>31)&1; flag_z=(" + dst + "==0); flag_c=_u>>32; flag_v=_s!=(int32_t)" + dst + "; }");
        else emit_line("}");
    } else if (inst.op == ir::IROperation::SUB) {
        emit_line(dst + " = " + a + " - " + b + ";");
        if (inst.update_flags) {
            emit_line("{ uint32_t _a=" + a + ", _b=" + b + ", _r=" + dst + "; flag_n=(_r>>31)&1; flag_z=(_r==0); flag_c=_a >= _b; flag_v=((_a ^ _b) & (_a ^ _r))>>31 &1; }");
        }
    } else if (inst.op == ir::IROperation::RSB) {
        // RSB: dest = b - a  but IR stores a=op2, b=rn => dest = a - b
        emit_line(dst + " = " + a + " - " + b + ";");
        if (inst.update_flags) {
            emit_line("{ uint32_t _a=" + a + ", _b=" + b + ", _r=" + dst + "; flag_n=(_r>>31)&1; flag_z=(_r==0); flag_c=_a >= _b; flag_v=((_a ^ _b) & (_a ^ _r))>>31 &1; }");
        }
    } else if (inst.op == ir::IROperation::SBC) {
        emit_line("{ bool _c_in=flag_c; uint64_t _u=(uint64_t)" + a + " + (uint64_t)(~" + b + ") + (_c_in?1:0); int64_t _s=(int64_t)(int32_t)" + a + " - (int64_t)(int32_t)" + b + " - (_c_in?0:1); " + dst + "=(uint32_t)_u;");
        if (inst.update_flags) emit_line("  flag_n=(" + dst + ">>31)&1; flag_z=(" + dst + "==0); flag_c=_u>>32; flag_v=_s!=(int32_t)" + dst + "; }");
        else emit_line("}");
    } else if (inst.op == ir::IROperation::RSC) {
        emit_line("{ bool _c_in=flag_c; uint64_t _u=(uint64_t)" + a + " + (uint64_t)(~" + b + ") + (_c_in?1:0); int64_t _s=(int64_t)(int32_t)" + a + " - (int64_t)(int32_t)" + b + " - (_c_in?0:1); " + dst + "=(uint32_t)_u;");
        if (inst.update_flags) emit_line("  flag_n=(" + dst + ">>31)&1; flag_z=(" + dst + "==0); flag_c=_u>>32; flag_v=_s!=(int32_t)" + dst + "; }");
        else emit_line("}");
    }
}

void CppEmitter::emit_logic(const ir::IRInstruction& inst) {
    std::string dst = value_expr(*inst.dest);
    std::string a = value_expr(inst.operands[0]);
    std::string b = value_expr(inst.operands[1]);
    std::string op_sym;
    switch (inst.op) {
        case ir::IROperation::AND: op_sym = " & "; break;
        case ir::IROperation::ORR: op_sym = " | "; break;
        case ir::IROperation::EOR: op_sym = " ^ "; break;
        case ir::IROperation::BIC: op_sym = " & ~"; break;
        default: op_sym = " & "; break;
    }
    if (inst.op == ir::IROperation::BIC) emit_line(dst + " = " + a + " & ~" + b + ";");
    else emit_line(dst + " = " + a + op_sym + b + ";");
    if (inst.update_flags) {
        emit_line("flag_n = (" + dst + " >> 31) & 1;");
        emit_line("flag_z = (" + dst + " == 0);");
    }
}

void CppEmitter::emit_cmp(const ir::IRInstruction& inst) {
    std::string a = value_expr(inst.operands[0]);
    std::string b = value_expr(inst.operands[1]);
    std::string res;
    if (inst.op == ir::IROperation::CMP) {
        res = "(" + a + " - " + b + ")";
        emit_line("{ uint32_t _res = " + res + "; flag_n = (_res >> 31) & 1; flag_z = (_res == 0); flag_c = (" + a + " >= " + b + "); flag_v = (((" + a + " ^ " + b + ") & (" + a + " ^ _res)) >> 31) & 1; }");
    } else if (inst.op == ir::IROperation::CMN) {
        res = "(" + a + " + " + b + ")";
        emit_line("{ uint32_t _res = " + res + "; flag_n = (_res >> 31) & 1; flag_z = (_res == 0); flag_c = (_res < " + a + "); flag_v = ((~(" + a + " ^ " + b + ") & (" + a + " ^ _res)) >> 31) & 1; }");
    } else if (inst.op == ir::IROperation::TST) {
        res = "(" + a + " & " + b + ")";
        emit_line("{ uint32_t _res = " + res + "; flag_n = (_res >> 31) & 1; flag_z = (_res == 0); }");
    } else if (inst.op == ir::IROperation::TEQ) {
        res = "(" + a + " ^ " + b + ")";
        emit_line("{ uint32_t _res = " + res + "; flag_n = (_res >> 31) & 1; flag_z = (_res == 0); }");
    }
}

void CppEmitter::emit_shift(const ir::IRInstruction& inst) {
    std::string dst = value_expr(*inst.dest);
    std::string val = value_expr(inst.operands[0]);
    std::string amt_expr = value_expr(inst.operands[1]);
    std::string fn;
    switch (inst.op) {
        case ir::IROperation::LSL: fn = "nds_lsl"; break;
        case ir::IROperation::LSR: fn = "nds_lsr"; break;
        case ir::IROperation::ASR: fn = "nds_asr"; break;
        case ir::IROperation::ROR: fn = "nds_ror"; break;
        default: fn = "nds_lsl"; break;
    }
    emit_line(dst + " = " + fn + "(" + val + ", " + amt_expr + ");");
    if (inst.update_flags) {
        emit_line("flag_n = (" + dst + " >> 31) & 1;");
        emit_line("flag_z = (" + dst + " == 0);");
        // C from shifter: precise for immediate amounts, approximate for register
        if (inst.operands[1].is_immediate()) {
            int64_t amt = inst.operands[1].immediate;
            if (amt == 0) {
                emit_line("// flag_c unchanged (shift #0)");
            } else {
                if (inst.op == ir::IROperation::LSL) {
                    emit_line("flag_c = (" + val + " >> (32 - " + std::to_string(amt) + ")) & 1;");
                } else if (inst.op == ir::IROperation::LSR) {
                    if (amt == 32) emit_line("flag_c = (" + val + " >> 31) & 1;");
                    else emit_line("flag_c = (" + val + " >> (" + std::to_string(amt-1) + ")) & 1;");
                } else if (inst.op == ir::IROperation::ASR) {
                    if (amt == 32 || amt >= 32) emit_line("flag_c = (" + val + " >> 31) & 1;");
                    else emit_line("flag_c = (" + val + " >> (" + std::to_string(amt-1) + ")) & 1;");
                } else if (inst.op == ir::IROperation::ROR) {
                    if (amt == 0) emit_line("flag_c = " + val + " & 1; // RRX");
                    else emit_line("flag_c = (" + val + " >> (" + std::to_string(amt-1) + ")) & 1;");
                }
            }
        } else {
            emit_line("// flag_c from register shift (approx)");
            emit_line("if ((" + amt_expr + " & 0xFF) != 0) flag_c = 0; // approx");
        }
    }
}

void CppEmitter::emit_mul(const ir::IRInstruction& inst) {
    std::string dst = value_expr(*inst.dest);
    if (inst.operands.size() == 3) {
        // MLA: d = m * s + n
        emit_line(dst + " = " + value_expr(inst.operands[0]) + " * " + value_expr(inst.operands[1]) + " + " + value_expr(inst.operands[2]) + ";");
    } else {
        emit_line(dst + " = " + value_expr(inst.operands[0]) + " * " + value_expr(inst.operands[1]) + ";");
    }
    if (inst.update_flags) {
        emit_line("flag_n = (" + dst + " >> 31) & 1;");
        emit_line("flag_z = (" + dst + " == 0);");
    }
}

void CppEmitter::emit_long_mul(const ir::IRInstruction& inst) {
    // dest = RdLo, dest2 = RdHi, operands = {Rm, Rs}
    std::string rd_lo = value_expr(*inst.dest);
    std::string rd_hi = inst.dest2 ? value_expr(*inst.dest2) : "/*RdHi*/0";
    std::string rm = value_expr(inst.operands[0]);
    std::string rs = value_expr(inst.operands[1]);
    bool is_signed = (inst.op == ir::IROperation::SMULL || inst.op == ir::IROperation::SMLAL);
    bool is_acc = (inst.op == ir::IROperation::UMLAL || inst.op == ir::IROperation::SMLAL);
    if (is_signed) {
        emit_line("{ int64_t _prod = (int64_t)(int32_t)" + rm + " * (int64_t)(int32_t)" + rs + ";");
    } else {
        emit_line("{ uint64_t _prod = (uint64_t)" + rm + " * (uint64_t)" + rs + ";");
    }
    indent_inc();
    if (is_acc) {
        if (is_signed) {
            emit_line("int64_t _acc = ((int64_t)" + rd_hi + " << 32) | (uint32_t)" + rd_lo + ";");
            emit_line("_prod += _acc;");
        } else {
            emit_line("uint64_t _acc = ((uint64_t)" + rd_hi + " << 32) | " + rd_lo + ";");
            emit_line("_prod += _acc;");
        }
    }
    emit_line(rd_lo + " = (uint32_t)_prod;");
    emit_line(rd_hi + " = (uint32_t)(_prod >> 32);");
    if (inst.update_flags) {
        emit_line("flag_n = (" + rd_hi + " >> 31) & 1;");
        emit_line("flag_z = (" + rd_lo + " == 0 && " + rd_hi + " == 0);");
    }
    indent_dec();
    emit_line("}");
}

void CppEmitter::emit_load(const ir::IRInstruction& inst) {
    std::string dst = value_expr(*inst.dest);
    const auto& mem = inst.operands[0].memory;
    std::string addr_expr = mem_addr_expr(mem);

    // Handle writeback / pre/post indexing comment and emit.
    if (mem.writeback || !mem.pre_indexed) {
        // We emit address calc and base update explicitly for traceability.
        emit_line("{ uint32_t _addr = " + addr_expr + ";");
        indent_inc();
        std::string base = reg_expr(mem.base_reg);
        if (!mem.pre_indexed) {
            // post-indexed: access base, then update base
            emit_line("// post-indexed: base=" + base + " addr=_addr then base+=offset");
        }
        // access
        if (mem.access == ir::IRMemoryAccess::Word) {
            if (mem.sign_extend) emit_line(dst + " = (int32_t)nds_hal_read32(_addr);");
            else emit_line(dst + " = nds_hal_read32(_addr);");
        } else if (mem.access == ir::IRMemoryAccess::HalfWord) {
            if (mem.sign_extend) emit_line(dst + " = (int32_t)(int16_t)nds_hal_read16(_addr);");
            else emit_line(dst + " = nds_hal_read16(_addr);");
        } else {
            if (mem.sign_extend) emit_line(dst + " = (int32_t)(int8_t)nds_hal_read8(_addr);");
            else emit_line(dst + " = nds_hal_read8(_addr);");
        }
        if (mem.writeback && mem.pre_indexed) emit_line(base + " = _addr;");
        else if (mem.writeback && !mem.pre_indexed) {
            // post-indexed writeback: base already held old value; compute new base
            std::string off = mem.index_reg != arm::REG_NONE ? value_expr(ir::IRValue::reg(mem.index_reg)) : std::to_string(mem.displacement);
            emit_line(base + " = _addr + " + off + "; // post-indexed writeback");
        }
        indent_dec();
        emit_line("}");
    } else {
        std::string addr = addr_expr;
        if (mem.access == ir::IRMemoryAccess::Word) {
            emit_line(dst + " = nds_hal_read32(" + addr + ");");
        } else if (mem.access == ir::IRMemoryAccess::HalfWord) {
            if (mem.sign_extend) emit_line(dst + " = (int32_t)(int16_t)nds_hal_read16(" + addr + ");");
            else emit_line(dst + " = nds_hal_read16(" + addr + ");");
        } else {
            if (mem.sign_extend) emit_line(dst + " = (int32_t)(int8_t)nds_hal_read8(" + addr + ");");
            else emit_line(dst + " = nds_hal_read8(" + addr + ");");
        }
    }
}

void CppEmitter::emit_store(const ir::IRInstruction& inst) {
    // dest is Memory, operands[0] is value reg
    const auto& mem = inst.dest->memory;
    std::string src = value_expr(inst.operands[0]);
    std::string addr_expr = mem_addr_expr(mem);

    if (mem.writeback || !mem.pre_indexed) {
        emit_line("{ uint32_t _addr = " + addr_expr + ";");
        indent_inc();
        if (mem.access == ir::IRMemoryAccess::Word) emit_line("nds_hal_write32(_addr, " + src + ");");
        else if (mem.access == ir::IRMemoryAccess::HalfWord) emit_line("nds_hal_write16(_addr, (uint16_t)" + src + ");");
        else emit_line("nds_hal_write8(_addr, (uint8_t)" + src + ");");
        std::string base = reg_expr(mem.base_reg);
        if (mem.writeback && mem.pre_indexed) emit_line(base + " = _addr;");
        else if (mem.writeback && !mem.pre_indexed) {
            std::string off = mem.index_reg != arm::REG_NONE ? value_expr(ir::IRValue::reg(mem.index_reg)) : std::to_string(mem.displacement);
            emit_line(base + " = " + base + " + " + off + ";");
        }
        indent_dec();
        emit_line("}");
    } else {
        if (mem.access == ir::IRMemoryAccess::Word) emit_line("nds_hal_write32(" + addr_expr + ", " + src + ");");
        else if (mem.access == ir::IRMemoryAccess::HalfWord) emit_line("nds_hal_write16(" + addr_expr + ", (uint16_t)" + src + ");");
        else emit_line("nds_hal_write8(" + addr_expr + ", (uint8_t)" + src + ");");
    }
}

void CppEmitter::emit_branch(const ir::IRInstruction& inst) {
    // Unconditional direct branch
    int64_t target = inst.operands[0].immediate;
    std::string label = "block_" + hex8(static_cast<uint32_t>(target)).substr(2); // without 0x
    // hex8 returns 0x..., substr(2) removes 0x
    label = "block_" + hex8(static_cast<uint32_t>(target));
    // hex8 already includes 0x, keep it? Use block_0200001C style
    // Do: block_0200001C
    std::ostringstream hs;
    hs << "block_" << std::hex << std::uppercase << std::setw(8) << std::setfill('0') << static_cast<uint32_t>(target);
    label = hs.str();
    emit_line("goto " + label + ";");
}

void CppEmitter::emit_cond_branch(const ir::IRInstruction& inst) {
    int64_t target = inst.operands[0].immediate;
    std::ostringstream hs;
    hs << "block_" << std::hex << std::uppercase << std::setw(8) << std::setfill('0') << static_cast<uint32_t>(target);
    std::string label = hs.str();
    std::string cond = condition_expr(*inst.condition);
    emit_line("if " + cond + " goto " + label + ";");
}

void CppEmitter::emit_call(const ir::IRInstruction& inst) {
    // Handle PC for call instruction itself if it uses PC
    // (already handled in emit_instruction)
    if (inst.is_indirect) {
        std::string target = value_expr(inst.operands[0]);
        // BLX Rm is a call that also saves LR
        bool is_blx = (inst.original_type == arm::InstructionType::BLX);
        if (is_blx) {
            uint32_t lr_val = inst.address + 4;
            if (inst.is_thumb) lr_val |= 1;
            emit_line("cpu.r[14] = " + hex8(lr_val) + "; // BLX saves LR");
        }
        emit_line("// BLX indirect via " + target);
        emit_line("{ uint32_t _target = " + target + "; cpu.thumb = (_target & 1); (void)_target; /* indirect call */ }");
        emit_line("flag_n = cpu.flag_n(); flag_z = cpu.flag_z(); flag_c = cpu.flag_c(); flag_v = cpu.flag_v();");
    } else {
        int64_t target = inst.operands[0].immediate;
        std::ostringstream hs;
        hs << "func_" << std::hex << std::uppercase << std::setw(8) << std::setfill('0') << static_cast<uint32_t>(target);
        std::string fname = hs.str();
        uint32_t lr_val = inst.address + 4;
        if (inst.is_thumb) lr_val |= 1;
        // For BLX direct, the target is Thumb, but we still call the C++ function
        emit_line("cpu.r[14] = " + hex8(lr_val) + "; // BL/BLX saves LR");
        // For BLX, also switch thumb mode for callee? The callee's thumb flag will be set via cpu.thumb
        if (inst.original_type == arm::InstructionType::BLX) {
            emit_line("cpu.thumb = true; // BLX to Thumb");
        }
        emit_line(fname + "(cpu);");
        emit_line("flag_n = cpu.flag_n(); flag_z = cpu.flag_z(); flag_c = cpu.flag_c(); flag_v = cpu.flag_v();");
        if (inst.original_type == arm::InstructionType::BLX) {
            emit_line("cpu.thumb = false; // return to ARM (approx)");
        }
    }
}

void CppEmitter::emit_return(const ir::IRInstruction& inst) {
    (void)inst;
    emit_line("_sync_flags();");
    emit_line("return;");
}

void CppEmitter::emit_indirect_branch(const ir::IRInstruction& inst) {
    std::string target = value_expr(inst.operands[0]);
    emit_line("// indirect branch to " + target);
    emit_line("{ uint32_t _t = " + target + "; cpu.thumb = (_t & 1); cpu.r[15] = _t & ~1u; }");
    emit_line("_sync_flags();");
    emit_line("return; // indirect branch (BX)");
}

void CppEmitter::emit_unknown(const ir::IRInstruction& inst) {
    emit_line("nds_unimplemented(" + hex8(inst.address) + ", " + hex8(inst.raw) + "); // " + inst.mnemonic);
}

void CppEmitter::emit_nop(const ir::IRInstruction& inst) {
    (void)inst;
    emit_line("(void)0; // NOP");
}

void CppEmitter::emit_clz(const ir::IRInstruction& inst) {
    std::string dst = value_expr(*inst.dest);
    std::string src = value_expr(inst.operands[0]);
    emit_line(dst + " = nds_clz(" + src + ");");
}

void CppEmitter::emit_mrs(const ir::IRInstruction& inst) {
    std::string dst = value_expr(*inst.dest);
    if (inst.spsr) {
        emit_line("// MRS SPSR not fully modeled; using CPSR");
        emit_line(dst + " = (flag_n<<31)|(flag_z<<30)|(flag_c<<29)|(flag_v<<28); // SPSR approx");
    } else {
        emit_line(dst + " = (flag_n<<31)|(flag_z<<30)|(flag_c<<29)|(flag_v<<28); // CPSR");
    }
}

void CppEmitter::emit_msr(const ir::IRInstruction& inst) {
    std::string src = value_expr(inst.operands[0]);
    if (inst.spsr) {
        emit_line("// MSR SPSR, " + src + " — SPSR write stub");
        emit_line("(void)" + src + ";");
    } else {
        // CPSR write: only flag field (f) affects N/Z/C/V. Check mask bit 3 (0x8)
        if (inst.msr_mask & 0x8) {
            emit_line("nds_msr_cpsr_flags(" + src + ", flag_n, flag_z, flag_c, flag_v);");
        } else {
            emit_line("// MSR CPSR without flag field (mask=" + std::to_string(inst.msr_mask) + ") — no flag update");
            emit_line("(void)" + src + ";");
        }
    }
}

void CppEmitter::emit_swi(const ir::IRInstruction& inst) {
    std::string imm = inst.operands.empty() ? "0" : value_expr(inst.operands[0]);
    emit_line("nds_swi(" + imm + "); // SWI " + hex8(inst.address));
}

void CppEmitter::emit_bkpt(const ir::IRInstruction& inst) {
    std::string imm = inst.operands.empty() ? "0" : value_expr(inst.operands[0]);
    emit_line("nds_bkpt(" + imm + "); // BKPT");
}

void CppEmitter::emit_instruction(const ir::IRInstruction& inst) {
    // Provenance comment
    std::string mode = inst.is_thumb ? "Thumb" : "ARM";
    emit_line("// " + hex8(inst.address) + " (" + mode + ") " + inst.mnemonic + " raw=" + hex_raw(inst.raw, inst.is_thumb));

    bool has_cond = inst.condition.has_value() && *inst.condition != arm::Condition::AL;
    if (has_cond) {
        std::string cond = condition_expr(*inst.condition);
        emit_line("if " + cond + " {");
        indent_inc();
    }

    if (uses_reg(inst, 15)) {
        uint32_t pc_val = inst.address + (inst.is_thumb ? 4 : 8);
        emit_line("cpu.r[15] = " + hex8(pc_val) + "; // PC semantics");
    }

    switch (inst.op) {
        case ir::IROperation::MOV:
        case ir::IROperation::MVN:
            emit_mov(inst);
            break;
        case ir::IROperation::ADD:
        case ir::IROperation::ADC:
        case ir::IROperation::SUB:
        case ir::IROperation::SBC:
        case ir::IROperation::RSB:
        case ir::IROperation::RSC:
            emit_arith(inst);
            break;
        case ir::IROperation::AND:
        case ir::IROperation::ORR:
        case ir::IROperation::EOR:
        case ir::IROperation::BIC:
            emit_logic(inst);
            break;
        case ir::IROperation::CMP:
        case ir::IROperation::CMN:
        case ir::IROperation::TST:
        case ir::IROperation::TEQ:
            emit_cmp(inst);
            break;
        case ir::IROperation::LSL:
        case ir::IROperation::LSR:
        case ir::IROperation::ASR:
        case ir::IROperation::ROR:
            emit_shift(inst);
            break;
        case ir::IROperation::MUL:
            emit_mul(inst);
            break;
        case ir::IROperation::UMULL:
        case ir::IROperation::UMLAL:
        case ir::IROperation::SMULL:
        case ir::IROperation::SMLAL:
            emit_long_mul(inst);
            break;
        case ir::IROperation::LOAD:
            emit_load(inst);
            break;
        case ir::IROperation::STORE:
            emit_store(inst);
            break;
        case ir::IROperation::CLZ:
            emit_clz(inst);
            break;
        case ir::IROperation::MRS:
            emit_mrs(inst);
            break;
        case ir::IROperation::MSR:
            emit_msr(inst);
            break;
        case ir::IROperation::SWI:
            emit_swi(inst);
            break;
        case ir::IROperation::BKPT:
            emit_bkpt(inst);
            break;
        case ir::IROperation::MRC:
        case ir::IROperation::MCR: {
            // Emit as a call to the runtime MRC/MCR handler
            std::string cp_num = value_expr(inst.operands[0]);
            std::string crn = value_expr(inst.operands[1]);
            std::string crm = value_expr(inst.operands[2]);
            std::string op2 = value_expr(inst.operands[3]);
            if (inst.op == ir::IROperation::MRC) {
                std::string dest = value_expr(*inst.dest);
                emit_line(dest + " = nds_mrc(" + cp_num + ", " + op2 + ", " + crn + ", " + crm + "); // " + inst.mnemonic);
            } else {
                std::string src = value_expr(inst.operands[0]);
                emit_line("nds_mcr(" + cp_num + ", " + op2 + ", " + src + ", " + crn + ", " + crm + "); // " + inst.mnemonic);
            }
            break;
        }
        case ir::IROperation::BRANCH:
            emit_branch(inst);
            break;
        case ir::IROperation::COND_BRANCH:
            emit_cond_branch(inst);
            break;
        case ir::IROperation::CALL:
            emit_call(inst);
            break;
        case ir::IROperation::RETURN:
            emit_return(inst);
            break;
        case ir::IROperation::INDIRECT_BRANCH:
            emit_indirect_branch(inst);
            break;
        case ir::IROperation::UNKNOWN:
            emit_unknown(inst);
            break;
        case ir::IROperation::NOP:
            emit_nop(inst);
            break;
        default:
            emit_unknown(inst);
            break;
    }

    if (has_cond) {
        indent_dec();
        emit_line("}");
    }
}

void CppEmitter::emit_block(const ir::IRBasicBlock& block) {
    std::ostringstream hs;
    hs << "block_" << std::hex << std::uppercase << std::setw(8) << std::setfill('0') << block.start;
    emit_line(hs.str() + ":");
    indent_inc();
    emit_line("// Block " + hex8(block.start) + " -> " + hex8(block.end) + " successors=" + std::to_string(block.successors.size()));
    if (block.is_thumb) emit_line("cpu.thumb = true;");
    else emit_line("cpu.thumb = false;");
    for (const auto& inst : block.instructions) {
        emit_instruction(inst);
    }
    // If block is not terminated and has a fallthrough successor, the next block label will be reached.
    indent_dec();
}

void CppEmitter::emit_function(const ir::IRFunction& fn) {
    std::ostringstream hs;
    hs << std::hex << std::uppercase << std::setw(8) << std::setfill('0') << fn.address;
    std::string func_name = "func_" + hs.str();
    std::string thumb_comment = fn.thumb ? " // Thumb" : " // ARM";

    emit_line("// Function " + hex8(fn.address) + thumb_comment + " blocks=" + std::to_string(fn.blocks.size()));
    emit_line("void " + func_name + "(descomp::runtime::CPUState& cpu) {");
    indent_inc();
    emit_line("bool flag_n = cpu.flag_n(); bool flag_z = cpu.flag_z(); bool flag_c = cpu.flag_c(); bool flag_v = cpu.flag_v();");
    emit_line("auto _sync_flags = [&](){ cpu.set_flags(flag_n, flag_z, flag_c, flag_v); };");

    // Declare temporaries used in this function.
    // Collect max temp id across blocks.
    uint32_t max_temp = 0;
    bool has_temp = false;
    for (auto& [addr, block] : fn.block_map) {
        for (auto& inst : block.instructions) {
            for (auto& op : inst.operands) if (op.is_temporary()) { has_temp = true; max_temp = std::max(max_temp, op.id); }
            if (inst.dest && inst.dest->is_temporary()) { has_temp = true; max_temp = std::max(max_temp, inst.dest->id); }
            if (inst.dest2 && inst.dest2->is_temporary()) { has_temp = true; max_temp = std::max(max_temp, inst.dest2->id); }
        }
    }
    if (has_temp) {
        std::ostringstream ts;
        ts << "uint32_t ";
        for (uint32_t i = 0; i <= max_temp; ++i) {
            if (i) ts << ", ";
            ts << "t" << i << " = 0";
        }
        ts << ";";
        emit_line(ts.str());
    }

    // Forward-declare callees for BL direct calls (so the file compiles standalone).
    for (uint32_t callee : fn.callees) {
        std::ostringstream cs;
        cs << "void func_" << std::hex << std::uppercase << std::setw(8) << std::setfill('0') << callee << "(descomp::runtime::CPUState&);";
        emit_line(cs.str());
    }

    for (uint32_t blk_addr : fn.blocks) {
        auto it = fn.block_map.find(blk_addr);
        if (it == fn.block_map.end()) continue;
        emit_block(it->second);
    }

    // Emit dummy stubs for any branch targets outside this function (to make C++ compile)
    // These are branches to addresses not in this function's block_map (e.g., cross-function or data)
    {
        std::set<uint32_t> outside_targets;
        for (auto& [addr, block] : fn.block_map) {
            for (auto& inst : block.instructions) {
                if (inst.op == ir::IROperation::BRANCH || inst.op == ir::IROperation::COND_BRANCH) {
                    uint32_t tgt = static_cast<uint32_t>(inst.operands[0].immediate);
                    if (fn.block_map.find(tgt) == fn.block_map.end()) {
                        // If target is a known function, it should be a CALL not a branch, skip
                        bool is_known_func = false;
                        // We don't have program here, so just treat all outside as stub
                        outside_targets.insert(tgt);
                    }
                }
            }
        }
        for (uint32_t tgt : outside_targets) {
            std::ostringstream hs;
            hs << std::hex << std::uppercase << std::setw(8) << std::setfill('0') << tgt;
            emit_line("block_" + hs.str() + ":");
            indent_inc();
            emit_line("// branch outside function to 0x" + hs.str() + " - stub");
            emit_line("nds_unimplemented(0x" + hs.str() + ", 0);");
            emit_line("return;");
            indent_dec();
        }
    }

    // Fallthrough return (if function doesn't end with explicit return, ensure it returns)
    emit_line("_sync_flags();");
    emit_line("return;");
    indent_dec();
    emit_line("}");
    emit_line("");
}

void CppEmitter::emit_program(const ir::IRProgram& prog) {
    emit_prelude();
    // Collect all function addresses for forward declarations of inter-function calls.
    // First pass: forward declare all funcs so intra-program calls compile.
    for (auto& [addr, fn] : prog.functions) {
        std::ostringstream hs;
        hs << "void func_" << std::hex << std::uppercase << std::setw(8) << std::setfill('0') << addr << "(descomp::runtime::CPUState&);";
        emit_line(hs.str());
    }
    if (!prog.functions.empty()) emit_line("");
    for (auto& [addr, fn] : prog.functions) {
        emit_function(fn);
    }
}

} // namespace descomp::lifter
