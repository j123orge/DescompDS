#include "arm/arm_decoder.h"
#include <sstream>
#include <iomanip>
#include <cstring>

namespace descomp::arm {

static std::string format_reg_list(uint16_t mask) {
    std::ostringstream ss;
    ss << "{";
    bool first = true;
    for (int i = 0; i < 16; ++i) {
        if (mask & (1 << i)) {
            if (!first) ss << ", ";
            ss << register_name(static_cast<uint8_t>(i));
            first = false;
        }
    }
    ss << "}";
    return ss.str();
}

static uint32_t ror32(uint32_t val, uint32_t rot) {
    rot &= 31;
    if (rot == 0) return val;
    return (val >> rot) | (val << (32 - rot));
}

ARMInstruction ARMDecoder::decode_branch(uint32_t raw, uint32_t address, Condition cond) {
    ARMInstruction inst;
    inst.address = address;
    inst.raw = raw;
    inst.condition = cond;
    inst.is_thumb = false;
    inst.is_branch = true;

    bool is_bl = (raw & (1 << 24)) != 0;
    int32_t offset = static_cast<int32_t>(raw & 0x00FFFFFF);
    // Sign-extend 24-bit offset to 32-bit
    if (offset & 0x00800000) {
        offset |= 0xFF000000;
    }
    offset <<= 2; // Word aligned offset

    // ARM PC is ahead by 8 bytes
    uint32_t target = address + 8 + static_cast<uint32_t>(offset);
    inst.branch_target = target;
    inst.immediate = offset;
    inst.has_immediate = true;

    std::string cond_str(condition_suffix(cond));

    if (cond == Condition::NV) {
        // BLX with immediate (ARMv5 unconditional)
        inst.type = InstructionType::BLX;
        inst.is_call = true;
        bool h = (raw & (1 << 24)) != 0;
        target = address + 8 + (static_cast<uint32_t>(offset) | (h ? 2 : 0));
        inst.branch_target = target;
        inst.mnemonic = "BLX";
    } else if (is_bl) {
        inst.type = InstructionType::BL;
        inst.is_call = true;
        inst.mnemonic = "BL" + cond_str;
    } else {
        inst.type = InstructionType::B;
        inst.mnemonic = "B" + cond_str;
        inst.is_conditional_branch = (cond != Condition::AL);
    }

    std::ostringstream ss;
    ss << "0x" << std::hex << std::uppercase << std::setw(8) << std::setfill('0') << target;
    inst.operands_str = ss.str();

    return inst;
}

ARMInstruction ARMDecoder::decode_data_processing(uint32_t raw, uint32_t address, Condition cond) {
    ARMInstruction inst;
    inst.address = address;
    inst.raw = raw;
    inst.condition = cond;
    inst.is_thumb = false;

    bool is_imm = (raw & (1 << 25)) != 0;
    uint8_t opcode = static_cast<uint8_t>((raw >> 21) & 0xF);
    bool s_bit = (raw & (1 << 20)) != 0;
    uint8_t rn = static_cast<uint8_t>((raw >> 16) & 0xF);
    uint8_t rd = static_cast<uint8_t>((raw >> 12) & 0xF);

    inst.rn = rn;
    inst.rd = rd;
    inst.sets_flags = s_bit;

    std::string op_name;
    bool is_comparison = false;
    bool has_rd = true;
    bool has_rn = true;

    switch (opcode) {
        case 0x0: inst.type = InstructionType::AND; op_name = "AND"; break;
        case 0x1: inst.type = InstructionType::EOR; op_name = "EOR"; break;
        case 0x2: inst.type = InstructionType::SUB; op_name = "SUB"; break;
        case 0x3: inst.type = InstructionType::RSB; op_name = "RSB"; break;
        case 0x4: inst.type = InstructionType::ADD; op_name = "ADD"; break;
        case 0x5: inst.type = InstructionType::ADC; op_name = "ADC"; break;
        case 0x6: inst.type = InstructionType::SBC; op_name = "SBC"; break;
        case 0x7: inst.type = InstructionType::RSC; op_name = "RSC"; break;
        case 0x8: inst.type = InstructionType::TST; op_name = "TST"; is_comparison = true; has_rd = false; break;
        case 0x9: inst.type = InstructionType::TEQ; op_name = "TEQ"; is_comparison = true; has_rd = false; break;
        case 0xA: inst.type = InstructionType::CMP; op_name = "CMP"; is_comparison = true; has_rd = false; break;
        case 0xB: inst.type = InstructionType::CMN; op_name = "CMN"; is_comparison = true; has_rd = false; break;
        case 0xC: inst.type = InstructionType::ORR; op_name = "ORR"; break;
        case 0xD: inst.type = InstructionType::MOV; op_name = "MOV"; has_rn = false; break;
        case 0xE: inst.type = InstructionType::BIC; op_name = "BIC"; break;
        case 0xF: inst.type = InstructionType::MVN; op_name = "MVN"; has_rn = false; break;
        default: break;
    }

    std::string cond_str(condition_suffix(cond));
    std::string s_str = (!is_comparison && s_bit) ? "S" : "";
    inst.mnemonic = op_name + cond_str + s_str;

    // Decode Op2
    std::string op2_str;
    if (is_imm) {
        uint32_t imm8 = raw & 0xFF;
        uint32_t rot = ((raw >> 8) & 0xF) * 2;
        uint32_t val = ror32(imm8, rot);
        inst.immediate = static_cast<int32_t>(val);
        inst.has_immediate = true;

        std::ostringstream ss;
        ss << "#" << val;
        op2_str = ss.str();
    } else {
        uint8_t rm = static_cast<uint8_t>(raw & 0xF);
        inst.rm = rm;
        uint8_t shift_type_raw = static_cast<uint8_t>((raw >> 5) & 0x3);
        inst.shift_type = static_cast<ShiftType>(shift_type_raw);
        bool reg_shift = (raw & (1 << 4)) != 0;

        if (reg_shift) {
            inst.has_reg_shift = true;
            inst.shift_reg = static_cast<uint8_t>((raw >> 8) & 0xF);
            std::ostringstream ss;
            ss << register_name(rm) << ", " << shift_name(inst.shift_type) << " " << register_name(inst.shift_reg);
            op2_str = ss.str();
        } else {
            uint8_t shift_amount = static_cast<uint8_t>((raw >> 7) & 0x1F);
            inst.shift_amount = shift_amount;
            std::ostringstream ss;
            ss << register_name(rm);
            if (shift_amount != 0 || (inst.shift_type != ShiftType::LSL && shift_amount == 0)) {
                inst.has_shift = true;
                ss << ", " << shift_name(inst.shift_type) << " #" << static_cast<int>(shift_amount);
            }
            op2_str = ss.str();
        }
    }

    // Check if writing to PC (e.g. MOV PC, LR => return)
    // For comparison instructions (CMP/TST/TEQ/CMN) Rd is not used, ignore Rd==PC
    if (has_rd && rd == Register::PC) {
        inst.is_branch = true;
        if (inst.type == InstructionType::MOV && inst.rm == Register::LR) {
            inst.is_return = true;
        } else {
            inst.is_indirect_branch = true;
        }
    }

    std::ostringstream ss;
    if (has_rd) {
        ss << register_name(rd);
        if (has_rn) {
            ss << ", " << register_name(rn);
        }
        ss << ", " << op2_str;
    } else {
        // Comparison instructions (CMP, TST, TEQ, CMN) have Rn, Op2
        ss << register_name(rn) << ", " << op2_str;
    }
    inst.operands_str = ss.str();

    return inst;
}

ARMInstruction ARMDecoder::decode_single_data_transfer(uint32_t raw, uint32_t address, Condition cond) {
    ARMInstruction inst;
    inst.address = address;
    inst.raw = raw;
    inst.condition = cond;
    inst.is_thumb = false;

    bool is_reg_offset = (raw & (1 << 25)) != 0;
    bool pre_index = (raw & (1 << 24)) != 0;
    bool add_offset = (raw & (1 << 23)) != 0;
    bool byte_transfer = (raw & (1 << 22)) != 0;
    bool writeback = !pre_index || ((raw & (1 << 21)) != 0);
    bool is_load = (raw & (1 << 20)) != 0;

    uint8_t rn = static_cast<uint8_t>((raw >> 16) & 0xF);
    uint8_t rd = static_cast<uint8_t>((raw >> 12) & 0xF);

    inst.rn = rn;
    inst.rd = rd;
    inst.pre_indexed = pre_index;
    inst.add_offset = add_offset;
    inst.writeback = writeback;

    std::string op_name;
    if (is_load) {
        inst.type = byte_transfer ? InstructionType::LDRB : InstructionType::LDR;
        op_name = byte_transfer ? "LDRB" : "LDR";
    } else {
        inst.type = byte_transfer ? InstructionType::STRB : InstructionType::STR;
        op_name = byte_transfer ? "STRB" : "STR";
    }

    std::string cond_str(condition_suffix(cond));
    inst.mnemonic = op_name + cond_str;

    std::string offset_str;
    if (!is_reg_offset) {
        int32_t offset = static_cast<int32_t>(raw & 0xFFF);
        inst.immediate = add_offset ? offset : -offset;
        inst.has_immediate = true;
        if (offset != 0) {
            std::ostringstream oss;
            oss << ", #" << (add_offset ? "" : "-") << offset;
            offset_str = oss.str();
        }
    } else {
        uint8_t rm = static_cast<uint8_t>(raw & 0xF);
        inst.rm = rm;
        uint8_t shift_type_raw = static_cast<uint8_t>((raw >> 5) & 0x3);
        inst.shift_type = static_cast<ShiftType>(shift_type_raw);
        uint8_t shift_amount = static_cast<uint8_t>((raw >> 7) & 0x1F);
        inst.shift_amount = shift_amount;

        std::ostringstream oss;
        oss << ", " << (add_offset ? "" : "-") << register_name(rm);
        if (shift_amount != 0) {
            oss << ", " << shift_name(inst.shift_type) << " #" << static_cast<int>(shift_amount);
        }
        offset_str = oss.str();
    }

    std::ostringstream ss;
    ss << register_name(rd) << ", [";
    ss << register_name(rn);
    if (pre_index) {
        ss << offset_str << "]";
        if (writeback) ss << "!";
    } else {
        ss << "]" << offset_str;
    }
    inst.operands_str = ss.str();

    // If loading into PC: LDR PC, [...] => indirect branch/return
    if (is_load && rd == Register::PC) {
        inst.is_branch = true;
        inst.is_indirect_branch = true;
    }

    return inst;
}

ARMInstruction ARMDecoder::decode_halfword_transfer(uint32_t raw, uint32_t address, Condition cond) {
    ARMInstruction inst;
    inst.address = address;
    inst.raw = raw;
    inst.condition = cond;
    inst.is_thumb = false;

    bool pre_index = (raw & (1 << 24)) != 0;
    bool add_offset = (raw & (1 << 23)) != 0;
    bool is_imm = (raw & (1 << 22)) != 0;
    bool writeback = !pre_index || ((raw & (1 << 21)) != 0);
    bool is_load = (raw & (1 << 20)) != 0;
    uint8_t rn = static_cast<uint8_t>((raw >> 16) & 0xF);
    uint8_t rd = static_cast<uint8_t>((raw >> 12) & 0xF);
    uint8_t op = static_cast<uint8_t>((raw >> 5) & 0x3); // 1=H, 2=SB, 3=SH

    inst.rn = rn;
    inst.rd = rd;
    inst.pre_indexed = pre_index;
    inst.add_offset = add_offset;
    inst.writeback = writeback;

    std::string op_name;
    if (is_load) {
        switch (op) {
            case 1: inst.type = InstructionType::LDRH; op_name = "LDRH"; break;
            case 2: inst.type = InstructionType::LDRSB; op_name = "LDRSB"; break;
            case 3: inst.type = InstructionType::LDRSH; op_name = "LDRSH"; break;
            default: inst.type = InstructionType::LDRH; op_name = "LDRH"; break;
        }
    } else {
        inst.type = InstructionType::STRH;
        op_name = "STRH";
    }

    std::string cond_str(condition_suffix(cond));
    inst.mnemonic = op_name + cond_str;

    std::string offset_str;
    if (is_imm) {
        uint32_t offset = ((raw >> 8) & 0xF) << 4 | (raw & 0xF);
        inst.immediate = add_offset ? static_cast<int32_t>(offset) : -static_cast<int32_t>(offset);
        inst.has_immediate = true;
        if (offset != 0) {
            std::ostringstream oss;
            oss << ", #" << (add_offset ? "" : "-") << offset;
            offset_str = oss.str();
        }
    } else {
        uint8_t rm = static_cast<uint8_t>(raw & 0xF);
        inst.rm = rm;
        std::ostringstream oss;
        oss << ", " << (add_offset ? "" : "-") << register_name(rm);
        offset_str = oss.str();
    }

    std::ostringstream ss;
    ss << register_name(rd) << ", [";
    ss << register_name(rn);
    if (pre_index) {
        ss << offset_str << "]";
        if (writeback) ss << "!";
    } else {
        ss << "]" << offset_str;
    }
    inst.operands_str = ss.str();

    return inst;
}

ARMInstruction ARMDecoder::decode_block_transfer(uint32_t raw, uint32_t address, Condition cond) {
    ARMInstruction inst;
    inst.address = address;
    inst.raw = raw;
    inst.condition = cond;
    inst.is_thumb = false;

    bool pre_index = (raw & (1 << 24)) != 0; // P
    bool add_offset = (raw & (1 << 23)) != 0; // U
    bool s_bit = (raw & (1 << 22)) != 0;     // S
    bool writeback = (raw & (1 << 21)) != 0; // W
    bool is_load = (raw & (1 << 20)) != 0;   // L
    uint8_t rn = static_cast<uint8_t>((raw >> 16) & 0xF);
    uint16_t reg_list = static_cast<uint16_t>(raw & 0xFFFF);

    inst.rn = rn;
    inst.register_list = reg_list;
    inst.pre_indexed = pre_index;
    inst.add_offset = add_offset;
    inst.writeback = writeback;
    inst.sets_flags = s_bit;

    std::string cond_str(condition_suffix(cond));

    // Special cases: PUSH and POP
    if (rn == Register::SP && is_load && !pre_index && add_offset && writeback) {
        // LDMIA SP!, {regs} => POP
        inst.type = InstructionType::POP;
        inst.mnemonic = "POP" + cond_str;
        inst.operands_str = format_reg_list(reg_list);
    } else if (rn == Register::SP && !is_load && pre_index && !add_offset && writeback) {
        // STMDB SP!, {regs} => PUSH
        inst.type = InstructionType::PUSH;
        inst.mnemonic = "PUSH" + cond_str;
        inst.operands_str = format_reg_list(reg_list);
    } else {
        std::string mode_str;
        if (add_offset) {
            mode_str = pre_index ? "IB" : "IA";
        } else {
            mode_str = pre_index ? "DB" : "DA";
        }

        if (is_load) {
            inst.type = InstructionType::LDM;
            inst.mnemonic = "LDM" + cond_str + mode_str;
        } else {
            inst.type = InstructionType::STM;
            inst.mnemonic = "STM" + cond_str + mode_str;
        }

        std::ostringstream ss;
        ss << register_name(rn) << (writeback ? "!" : "") << ", " << format_reg_list(reg_list);
        if (s_bit) ss << "^";
        inst.operands_str = ss.str();
    }

    // If loading into PC: LDMIA ... {..., PC} or POP {..., PC} => return
    if (is_load && (reg_list & (1 << Register::PC))) {
        inst.is_branch = true;
        inst.is_return = true;
    }

    return inst;
}

ARMInstruction ARMDecoder::decode_multiply(uint32_t raw, uint32_t address, Condition cond) {
    ARMInstruction inst;
    inst.address = address;
    inst.raw = raw;
    inst.condition = cond;
    inst.is_thumb = false;

    bool is_long = (raw & (1 << 23)) != 0;
    bool is_signed = (raw & (1 << 22)) != 0;
    bool accumulate = (raw & (1 << 21)) != 0;
    bool s_bit = (raw & (1 << 20)) != 0;
    inst.sets_flags = s_bit;

    uint8_t rd = static_cast<uint8_t>((raw >> 16) & 0xF); // Rd or RdHi
    uint8_t rn = static_cast<uint8_t>((raw >> 12) & 0xF); // Rn or RdLo
    uint8_t rs = static_cast<uint8_t>((raw >> 8) & 0xF);
    uint8_t rm = static_cast<uint8_t>(raw & 0xF);

    inst.rd = rd;
    inst.rn = rn;
    inst.rs = rs;
    inst.rm = rm;

    std::string op_name;
    if (is_long) {
        if (is_signed) {
            inst.type = accumulate ? InstructionType::SMLAL : InstructionType::SMULL;
            op_name = accumulate ? "SMLAL" : "SMULL";
        } else {
            inst.type = accumulate ? InstructionType::UMLAL : InstructionType::UMULL;
            op_name = accumulate ? "UMLAL" : "UMULL";
        }
    } else {
        inst.type = accumulate ? InstructionType::MLA : InstructionType::MUL;
        op_name = accumulate ? "MLA" : "MUL";
    }

    std::string cond_str(condition_suffix(cond));
    std::string s_str = s_bit ? "S" : "";
    inst.mnemonic = op_name + cond_str + s_str;

    std::ostringstream ss;
    if (is_long) {
        // RdLo, RdHi, Rm, Rs
        ss << register_name(rn) << ", " << register_name(rd) << ", " << register_name(rm) << ", " << register_name(rs);
    } else {
        if (accumulate) {
            // Rd, Rm, Rs, Rn
            ss << register_name(rd) << ", " << register_name(rm) << ", " << register_name(rs) << ", " << register_name(rn);
        } else {
            // Rd, Rm, Rs
            ss << register_name(rd) << ", " << register_name(rm) << ", " << register_name(rs);
        }
    }
    inst.operands_str = ss.str();

    return inst;
}

ARMInstruction ARMDecoder::decode_misc(uint32_t raw, uint32_t address, Condition cond) {
    ARMInstruction inst;
    inst.address = address;
    inst.raw = raw;
    inst.condition = cond;
    inst.is_thumb = false;

    std::string cond_str(condition_suffix(cond));

    // BX / BLX Register: 0x012FFF10 / 0x012FFF30
    if ((raw & 0x0FFFFFF0) == 0x012FFF10) {
        uint8_t rm = static_cast<uint8_t>(raw & 0xF);
        inst.type = InstructionType::BX;
        inst.rm = rm;
        inst.is_branch = true;
        inst.mnemonic = "BX" + cond_str;
        inst.operands_str = register_name(rm);
        if (rm == Register::LR) {
            inst.is_return = true;
        } else {
            inst.is_indirect_branch = true;
        }
        return inst;
    }

    if ((raw & 0x0FFFFFF0) == 0x012FFF30) {
        uint8_t rm = static_cast<uint8_t>(raw & 0xF);
        inst.type = InstructionType::BLX;
        inst.rm = rm;
        inst.is_branch = true;
        inst.is_call = true;
        inst.is_indirect_branch = true;
        inst.mnemonic = "BLX" + cond_str;
        inst.operands_str = register_name(rm);
        return inst;
    }

    // CLZ (Count Leading Zeros): cond 0001 0110 1111 Rd 1111 0001 Rm (0x016F0F10)
    if ((raw & 0x0FFF0FF0) == 0x016F0F10) {
        uint8_t rd = static_cast<uint8_t>((raw >> 12) & 0xF);
        uint8_t rm = static_cast<uint8_t>(raw & 0xF);
        inst.type = InstructionType::CLZ;
        inst.rd = rd;
        inst.rm = rm;
        inst.mnemonic = "CLZ" + cond_str;
        inst.operands_str = std::string(register_name(rd)) + ", " + std::string(register_name(rm));
        return inst;
    }

    // SWI / SVC: cond 1111 xxxx
    if ((raw & 0x0F000000) == 0x0F000000) {
        uint32_t comment = raw & 0x00FFFFFF;
        inst.type = InstructionType::SWI;
        inst.immediate = static_cast<int32_t>(comment);
        inst.has_immediate = true;
        inst.mnemonic = "SWI" + cond_str;
        std::ostringstream ss;
        ss << "#0x" << std::hex << comment;
        inst.operands_str = ss.str();
        return inst;
    }

    // BKPT: 0xE1200070
    if ((raw & 0xFFF000F0) == 0xE1200070) {
        uint32_t imm16 = ((raw >> 8) & 0xFFF0) | (raw & 0xF);
        inst.type = InstructionType::BKPT;
        inst.immediate = static_cast<int32_t>(imm16);
        inst.has_immediate = true;
        inst.mnemonic = "BKPT";
        std::ostringstream ss;
        ss << "#0x" << std::hex << imm16;
        inst.operands_str = ss.str();
        return inst;
    }

    // MRS / MSR
    if ((raw & 0x0FBF0FFF) == 0x010F0000) {
        // MRS Rd, CPSR/SPSR
        uint8_t rd = static_cast<uint8_t>((raw >> 12) & 0xF);
        bool use_spsr = (raw & (1 << 22)) != 0;
        inst.type = InstructionType::MRS;
        inst.rd = rd;
        inst.spsr = use_spsr;
        inst.mnemonic = "MRS" + cond_str;
        inst.operands_str = std::string(register_name(rd)) + ", " + (use_spsr ? "SPSR" : "CPSR");
        return inst;
    }

    if ((raw & 0x0DB0F000) == 0x0120F000) {
        // MSR CPSR/SPSR, Rm
        bool use_spsr = (raw & (1 << 22)) != 0;
        uint8_t rm = static_cast<uint8_t>(raw & 0xF);
        uint8_t mask = static_cast<uint8_t>((raw >> 16) & 0xF);
        inst.type = InstructionType::MSR;
        inst.rm = rm;
        inst.spsr = use_spsr;
        inst.msr_mask = mask;
        inst.mnemonic = "MSR" + cond_str;
        inst.operands_str = std::string(use_spsr ? "SPSR" : "CPSR") + ", " + std::string(register_name(rm));
        return inst;
    }

    // Also handle MSR with immediate (ARMv5): 0x0320F000 pattern (I=1)
    if ((raw & 0x0DB0F000) == 0x0320F000) {
        bool use_spsr = (raw & (1 << 22)) != 0;
        uint8_t mask = static_cast<uint8_t>((raw >> 16) & 0xF);
        uint32_t imm8 = raw & 0xFF;
        uint32_t rot = ((raw >> 8) & 0xF) * 2;
        uint32_t val = ror32(imm8, rot);
        inst.type = InstructionType::MSR;
        inst.immediate = static_cast<int32_t>(val);
        inst.has_immediate = true;
        inst.spsr = use_spsr;
        inst.msr_mask = mask;
        inst.mnemonic = "MSR" + cond_str;
        std::ostringstream ss;
        ss << (use_spsr ? "SPSR" : "CPSR") << ", #" << val;
        inst.operands_str = ss.str();
        return inst;
    }

    // NOP (MOV R0, R0 or specialized)
    if (raw == 0xE1A00000) {
        inst.type = InstructionType::NOP;
        inst.mnemonic = "NOP";
        inst.operands_str = "";
        return inst;
    }

    // MRC/MCR: Coprocessor register transfer
    // bits [27:24] == 0b1110, bit [4] == 1
    if ((raw & 0x0F000010) == 0x0E000010) {
        bool is_mrc = (raw & (1 << 20)) != 0; // L bit
        uint8_t crn = static_cast<uint8_t>((raw >> 16) & 0xF);
        uint8_t rd  = static_cast<uint8_t>((raw >> 12) & 0xF);
        uint8_t cp  = static_cast<uint8_t>((raw >> 8) & 0xF);
        uint8_t cp_op2 = static_cast<uint8_t>((raw >> 5) & 0x7);
        uint8_t crm = static_cast<uint8_t>(raw & 0xF);
        inst.type = is_mrc ? InstructionType::MRC : InstructionType::MCR;
        inst.rd = rd;
        inst.crn = crn;
        inst.crm = crm;
        inst.cp_num = cp;
        inst.cp_op2 = cp_op2;
        inst.cp_op1 = is_mrc ? 0 : static_cast<uint8_t>((raw >> 21) & 0x7);
        std::ostringstream ss;
        if (is_mrc) {
            inst.mnemonic = "MRC" + cond_str;
            ss << "P" << (int)cp << ", #" << (int)cp_op2 << ", "
               << register_name(rd) << ", C" << (int)crn << ", C" << (int)crm;
        } else {
            inst.mnemonic = "MCR" + cond_str;
            ss << "P" << (int)cp << ", #" << (int)cp_op2 << ", "
               << register_name(rd) << ", C" << (int)crn << ", C" << (int)crm;
        }
        inst.operands_str = ss.str();
        return inst;
    }

    // Unknown instruction fallback
    inst.type = InstructionType::UNKNOWN;
    inst.mnemonic = "DCD";
    std::ostringstream ss;
    ss << "0x" << std::hex << std::uppercase << std::setw(8) << std::setfill('0') << raw;
    inst.operands_str = ss.str();
    return inst;
}

ARMInstruction ARMDecoder::decode(uint32_t raw_instruction, uint32_t address) {
    Condition cond = static_cast<Condition>((raw_instruction >> 28) & 0xF);

    // Unconditional BLX (ARMv5, cond == 0xF)
    if (cond == Condition::NV) {
        if ((raw_instruction & 0xFE000000) == 0xFA000000) {
            return decode_branch(raw_instruction, address, cond);
        }
    }

    // Branch / Branch with Link (bits [27:25] == 0b101)
    if ((raw_instruction & 0x0E000000) == 0x0A000000) {
        return decode_branch(raw_instruction, address, cond);
    }

    // Block Data Transfer (LDM / STM) (bits [27:25] == 0b100)
    if ((raw_instruction & 0x0E000000) == 0x08000000) {
        return decode_block_transfer(raw_instruction, address, cond);
    }

    // Single Data Transfer (LDR / STR) (bits [27:26] == 0b01)
    if ((raw_instruction & 0x0C000000) == 0x04000000) {
        return decode_single_data_transfer(raw_instruction, address, cond);
    }

    // Halfword / Signed byte transfer (bits [27:25] == 0b000 && bit 7 == 1 && bit 4 == 1)
    if ((raw_instruction & 0x0E000090) == 0x00000090) {
        uint8_t op = static_cast<uint8_t>((raw_instruction >> 5) & 0x3);
        if (op != 0) {
            return decode_halfword_transfer(raw_instruction, address, cond);
        }
    }

    // Multiply / Multiply Accumulate (bits [27:24] == 0b0000 && bits [7:4] == 0b1001)
    if ((raw_instruction & 0x0F0000F0) == 0x00000090) {
        return decode_multiply(raw_instruction, address, cond);
    }

    // Misc instructions (BX, BLX, CLZ, SWI, MRS, MSR, BKPT)
    if ((raw_instruction & 0x0F000000) == 0x0F000000 || // SWI
        (raw_instruction & 0x0FBF0000) == 0x010F0000 || // MRS/MSR (reg)
        (raw_instruction & 0x0DB0F000) == 0x0320F000 || // MSR imm
        (raw_instruction & 0x0FFFFFF0) == 0x012FFF10 || // BX
        (raw_instruction & 0x0FFFFFF0) == 0x012FFF30 || // BLX
        (raw_instruction & 0x0FFF0FF0) == 0x016F0F10 || // CLZ
        (raw_instruction & 0x0FF000F0) == 0x01200070)   // BKPT
    {
        return decode_misc(raw_instruction, address, cond);
    }

    // Data Processing (bits [27:26] == 0b00)
    if ((raw_instruction & 0x0C000000) == 0x00000000) {
        return decode_data_processing(raw_instruction, address, cond);
    }

    return decode_misc(raw_instruction, address, cond);
}

std::vector<ARMInstruction> ARMDecoder::decode_buffer(std::span<const uint8_t> buffer, uint32_t base_address) {
    std::vector<ARMInstruction> result;
    size_t count = buffer.size() / 4;
    result.reserve(count);

    for (size_t i = 0; i < count; ++i) {
        uint32_t raw = 0;
        std::memcpy(&raw, buffer.data() + (i * 4), 4);
        uint32_t addr = base_address + static_cast<uint32_t>(i * 4);
        result.push_back(decode(raw, addr));
    }

    return result;
}

} // namespace descomp::arm
