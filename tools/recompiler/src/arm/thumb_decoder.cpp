#include "arm/thumb_decoder.h"
#include <sstream>
#include <iomanip>
#include <cstring>

namespace descomp::arm {

static std::string format_thumb_reg_list(uint8_t rlist, bool extra_reg, bool is_pop) {
    std::ostringstream ss;
    ss << "{";
    bool first = true;
    for (int i = 0; i < 8; ++i) {
        if (rlist & (1 << i)) {
            if (!first) ss << ", ";
            ss << register_name(static_cast<uint8_t>(i));
            first = false;
        }
    }
    if (extra_reg) {
        if (!first) ss << ", ";
        ss << (is_pop ? "pc" : "lr");
    }
    ss << "}";
    return ss.str();
}

ARMInstruction ThumbDecoder::decode(uint16_t raw, uint32_t address, uint16_t next_raw) {
    ARMInstruction inst;
    inst.address = address;
    inst.raw = raw;
    inst.is_thumb = true;
    inst.condition = Condition::AL;

    // 1. Shift by immediate (Format 1: 000xx)
    if ((raw & 0xE000) == 0x0000 && ((raw & 0x1800) != 0x1800)) {
        uint8_t op = static_cast<uint8_t>((raw >> 11) & 0x3);
        uint8_t imm5 = static_cast<uint8_t>((raw >> 6) & 0x1F);
        uint8_t rs = static_cast<uint8_t>((raw >> 3) & 0x7);
        uint8_t rd = static_cast<uint8_t>(raw & 0x7);

        inst.rd = rd;
        inst.rm = rs;
        inst.has_shift = true;
        inst.sets_flags = true;
        // LSR/ASR #0 means #32 per ARM spec
        if (op == 1 && imm5 == 0) inst.shift_amount = 32;
        else if (op == 2 && imm5 == 0) inst.shift_amount = 32;
        else inst.shift_amount = imm5;

        std::string op_name;
        switch (op) {
            case 0: inst.type = InstructionType::LSL; inst.shift_type = ShiftType::LSL; op_name = "LSL"; break;
            case 1: inst.type = InstructionType::LSR; inst.shift_type = ShiftType::LSR; op_name = "LSR"; break;
            case 2: inst.type = InstructionType::ASR; inst.shift_type = ShiftType::ASR; op_name = "ASR"; break;
            default: break;
        }
        inst.mnemonic = op_name;
        std::ostringstream ss;
        ss << register_name(rd) << ", " << register_name(rs) << ", #" << static_cast<int>(inst.shift_amount);
        inst.operands_str = ss.str();
        return inst;
    }

    // 2. Add / Subtract (Format 2: 00011x)
    if ((raw & 0xF800) == 0x1800) {
        bool is_imm = (raw & (1 << 10)) != 0;
        bool is_sub = (raw & (1 << 9)) != 0;
        uint8_t rn_or_imm = static_cast<uint8_t>((raw >> 6) & 0x7);
        uint8_t rs = static_cast<uint8_t>((raw >> 3) & 0x7);
        uint8_t rd = static_cast<uint8_t>(raw & 0x7);

        inst.rd = rd;
        inst.rn = rs;
        inst.sets_flags = true;
        inst.type = is_sub ? InstructionType::SUB : InstructionType::ADD;
        inst.mnemonic = is_sub ? "SUB" : "ADD";

        std::ostringstream ss;
        ss << register_name(rd) << ", " << register_name(rs) << ", ";
        if (is_imm) {
            inst.immediate = rn_or_imm;
            inst.has_immediate = true;
            ss << "#" << static_cast<int>(rn_or_imm);
        } else {
            inst.rm = rn_or_imm;
            ss << register_name(rn_or_imm);
        }
        inst.operands_str = ss.str();
        return inst;
    }

    // 3. Move / Compare / Add / Subtract Immediate (Format 3: 001xx)
    if ((raw & 0xE000) == 0x2000) {
        uint8_t op = static_cast<uint8_t>((raw >> 11) & 0x3);
        uint8_t rd = static_cast<uint8_t>((raw >> 8) & 0x7);
        uint8_t imm8 = static_cast<uint8_t>(raw & 0xFF);

        inst.rd = rd;
        inst.immediate = imm8;
        inst.has_immediate = true;
        inst.sets_flags = true;

        std::ostringstream ss;
        switch (op) {
            case 0:
                inst.type = InstructionType::MOV;
                inst.mnemonic = "MOV";
                ss << register_name(rd) << ", #" << static_cast<int>(imm8);
                break;
            case 1:
                inst.type = InstructionType::CMP;
                inst.mnemonic = "CMP";
                ss << register_name(rd) << ", #" << static_cast<int>(imm8);
                break;
            case 2:
                inst.type = InstructionType::ADD;
                inst.mnemonic = "ADD";
                ss << register_name(rd) << ", #" << static_cast<int>(imm8);
                break;
            case 3:
                inst.type = InstructionType::SUB;
                inst.mnemonic = "SUB";
                ss << register_name(rd) << ", #" << static_cast<int>(imm8);
                break;
        }
        inst.operands_str = ss.str();
        return inst;
    }

    // 4. ALU operations (Format 4: 010000xxxx)
    if ((raw & 0xFC00) == 0x4000) {
        uint8_t op = static_cast<uint8_t>((raw >> 6) & 0xF);
        uint8_t rs = static_cast<uint8_t>((raw >> 3) & 0x7);
        uint8_t rd = static_cast<uint8_t>(raw & 0x7);

        inst.rd = rd;
        inst.rm = rs;
        inst.sets_flags = true;

        std::string op_name;
        switch (op) {
            case 0x0: inst.type = InstructionType::AND; op_name = "AND"; break;
            case 0x1: inst.type = InstructionType::EOR; op_name = "EOR"; break;
            case 0x2: inst.type = InstructionType::LSL; op_name = "LSL"; inst.has_reg_shift = true; inst.shift_type = ShiftType::LSL; break;
            case 0x3: inst.type = InstructionType::LSR; op_name = "LSR"; inst.has_reg_shift = true; inst.shift_type = ShiftType::LSR; break;
            case 0x4: inst.type = InstructionType::ASR; op_name = "ASR"; inst.has_reg_shift = true; inst.shift_type = ShiftType::ASR; break;
            case 0x5: inst.type = InstructionType::ADC; op_name = "ADC"; break;
            case 0x6: inst.type = InstructionType::SBC; op_name = "SBC"; break;
            case 0x7: inst.type = InstructionType::ROR; op_name = "ROR"; inst.has_reg_shift = true; inst.shift_type = ShiftType::ROR; break;
            case 0x8: inst.type = InstructionType::TST; op_name = "TST"; break;
            case 0x9: inst.type = InstructionType::RSB; op_name = "NEG"; break;
            case 0xA: inst.type = InstructionType::CMP; op_name = "CMP"; break;
            case 0xB: inst.type = InstructionType::CMN; op_name = "CMN"; break;
            case 0xC: inst.type = InstructionType::ORR; op_name = "ORR"; break;
            case 0xD: inst.type = InstructionType::MUL; op_name = "MUL"; break;
            case 0xE: inst.type = InstructionType::BIC; op_name = "BIC"; break;
            case 0xF: inst.type = InstructionType::MVN; op_name = "MVN"; break;
        }

        inst.mnemonic = op_name;
        std::ostringstream ss;
        ss << register_name(rd) << ", " << register_name(rs);
        inst.operands_str = ss.str();
        return inst;
    }

    // 5. Hi register ops / Branch exchange (Format 5: 010001xxxx)
    if ((raw & 0xFC00) == 0x4400) {
        uint8_t op = static_cast<uint8_t>((raw >> 8) & 0x3);
        bool h1 = (raw & (1 << 7)) != 0;
        bool h2 = (raw & (1 << 6)) != 0;
        uint8_t rs = static_cast<uint8_t>((raw >> 3) & 0x7) | (h2 ? 8 : 0);
        uint8_t rd = static_cast<uint8_t>(raw & 0x7) | (h1 ? 8 : 0);

        inst.rd = rd;
        inst.rm = rs;

        std::ostringstream ss;
        if (op == 0) {
            inst.type = InstructionType::ADD;
            inst.mnemonic = "ADD";
            ss << register_name(rd) << ", " << register_name(rs);
        } else if (op == 1) {
            inst.type = InstructionType::CMP;
            inst.mnemonic = "CMP";
            inst.sets_flags = true;
            ss << register_name(rd) << ", " << register_name(rs);
        } else if (op == 2) {
            inst.type = InstructionType::MOV;
            inst.mnemonic = "MOV";
            ss << register_name(rd) << ", " << register_name(rs);
            if (rd == Register::PC) {
                inst.is_branch = true;
                if (rs == Register::LR) {
                    inst.is_return = true;
                } else {
                    inst.is_indirect_branch = true;
                }
            }
        } else if (op == 3) {
            if (h1) {
                inst.type = InstructionType::BLX;
                inst.mnemonic = "BLX";
                inst.is_call = true;
            } else {
                inst.type = InstructionType::BX;
                inst.mnemonic = "BX";
                if (rs == Register::LR) {
                    inst.is_return = true;
                }
            }
            inst.is_branch = true;
            inst.is_indirect_branch = true;
            ss << register_name(rs);
        }
        inst.operands_str = ss.str();
        return inst;
    }

    // 6. PC-relative Load (Format 6: 01001xxx)
    if ((raw & 0xF800) == 0x4800) {
        uint8_t rd = static_cast<uint8_t>((raw >> 8) & 0x7);
        uint32_t imm8 = raw & 0xFF;
        uint32_t offset = imm8 * 4;

        inst.type = InstructionType::LDR;
        inst.rd = rd;
        inst.rn = Register::PC;
        inst.immediate = static_cast<int32_t>(offset);
        inst.has_immediate = true;
        inst.mnemonic = "LDR";

        uint32_t target_addr = ((address + 4) & ~3) + offset;
        std::ostringstream ss;
        ss << register_name(rd) << ", [pc, #" << offset << "] ; =0x" << std::hex << target_addr;
        inst.operands_str = ss.str();
        return inst;
    }

    // 7 & 8. Load / Store with register offset (Format 7 & 8: 0101xxxxxx)
    if ((raw & 0xF200) == 0x5000) {
        uint8_t op = static_cast<uint8_t>((raw >> 9) & 0x7);
        uint8_t ro = static_cast<uint8_t>((raw >> 6) & 0x7);
        uint8_t rb = static_cast<uint8_t>((raw >> 3) & 0x7);
        uint8_t rd = static_cast<uint8_t>(raw & 0x7);

        inst.rd = rd;
        inst.rn = rb;
        inst.rm = ro;

        std::string op_name;
        switch (op) {
            case 0: inst.type = InstructionType::STR; op_name = "STR"; break;
            case 1: inst.type = InstructionType::STRH; op_name = "STRH"; break;
            case 2: inst.type = InstructionType::STRB; op_name = "STRB"; break;
            case 3: inst.type = InstructionType::LDRSB; op_name = "LDRSB"; break;
            case 4: inst.type = InstructionType::LDR; op_name = "LDR"; break;
            case 5: inst.type = InstructionType::LDRH; op_name = "LDRH"; break;
            case 6: inst.type = InstructionType::LDRB; op_name = "LDRB"; break;
            case 7: inst.type = InstructionType::LDRSH; op_name = "LDRSH"; break;
        }

        inst.mnemonic = op_name;
        std::ostringstream ss;
        ss << register_name(rd) << ", [" << register_name(rb) << ", " << register_name(ro) << "]";
        inst.operands_str = ss.str();
        return inst;
    }

    // 9. Load / Store with immediate offset (Format 9: 011xxxxx)
    if ((raw & 0xE000) == 0x6000) {
        bool is_byte = (raw & (1 << 12)) != 0;
        bool is_load = (raw & (1 << 11)) != 0;
        uint32_t imm5 = (raw >> 6) & 0x1F;
        uint8_t rb = static_cast<uint8_t>((raw >> 3) & 0x7);
        uint8_t rd = static_cast<uint8_t>(raw & 0x7);

        uint32_t offset = is_byte ? imm5 : (imm5 * 4);
        inst.rd = rd;
        inst.rn = rb;
        inst.immediate = static_cast<int32_t>(offset);
        inst.has_immediate = true;

        if (is_load) {
            inst.type = is_byte ? InstructionType::LDRB : InstructionType::LDR;
            inst.mnemonic = is_byte ? "LDRB" : "LDR";
        } else {
            inst.type = is_byte ? InstructionType::STRB : InstructionType::STR;
            inst.mnemonic = is_byte ? "STRB" : "STR";
        }

        std::ostringstream ss;
        ss << register_name(rd) << ", [" << register_name(rb);
        if (offset != 0) ss << ", #" << offset;
        ss << "]";
        inst.operands_str = ss.str();
        return inst;
    }

    // 10. Load / Store Halfword (Format 10: 1000xxxx)
    if ((raw & 0xF000) == 0x8000) {
        bool is_load = (raw & (1 << 11)) != 0;
        uint32_t imm5 = (raw >> 6) & 0x1F;
        uint8_t rb = static_cast<uint8_t>((raw >> 3) & 0x7);
        uint8_t rd = static_cast<uint8_t>(raw & 0x7);

        uint32_t offset = imm5 * 2;
        inst.rd = rd;
        inst.rn = rb;
        inst.immediate = static_cast<int32_t>(offset);
        inst.has_immediate = true;

        inst.type = is_load ? InstructionType::LDRH : InstructionType::STRH;
        inst.mnemonic = is_load ? "LDRH" : "STRH";

        std::ostringstream ss;
        ss << register_name(rd) << ", [" << register_name(rb);
        if (offset != 0) ss << ", #" << offset;
        ss << "]";
        inst.operands_str = ss.str();
        return inst;
    }

    // 11. SP-relative Load / Store (Format 11: 1001xxxx)
    if ((raw & 0xF000) == 0x9000) {
        bool is_load = (raw & (1 << 11)) != 0;
        uint8_t rd = static_cast<uint8_t>((raw >> 8) & 0x7);
        uint32_t imm8 = raw & 0xFF;
        uint32_t offset = imm8 * 4;

        inst.rd = rd;
        inst.rn = Register::SP;
        inst.immediate = static_cast<int32_t>(offset);
        inst.has_immediate = true;

        inst.type = is_load ? InstructionType::LDR : InstructionType::STR;
        inst.mnemonic = is_load ? "LDR" : "STR";

        std::ostringstream ss;
        ss << register_name(rd) << ", [sp";
        if (offset != 0) ss << ", #" << offset;
        ss << "]";
        inst.operands_str = ss.str();
        return inst;
    }

    // 12. Load Address / Add to SP or PC (Format 12: 1010xxxx)
    if ((raw & 0xF000) == 0xA000) {
        bool is_sp = (raw & (1 << 11)) != 0;
        uint8_t rd = static_cast<uint8_t>((raw >> 8) & 0x7);
        uint32_t imm8 = raw & 0xFF;
        uint32_t offset = imm8 * 4;

        inst.type = InstructionType::ADD;
        inst.rd = rd;
        inst.rn = is_sp ? Register::SP : Register::PC;
        inst.immediate = static_cast<int32_t>(offset);
        inst.has_immediate = true;
        inst.mnemonic = "ADD";

        std::ostringstream ss;
        ss << register_name(rd) << ", " << (is_sp ? "sp" : "pc") << ", #" << offset;
        inst.operands_str = ss.str();
        return inst;
    }

    // 13. Add / Subtract SP (Format 13: 10110000x)
    if ((raw & 0xFF00) == 0xB000) {
        bool is_sub = (raw & (1 << 7)) != 0;
        uint32_t imm7 = raw & 0x7F;
        uint32_t offset = imm7 * 4;

        inst.type = is_sub ? InstructionType::SUB : InstructionType::ADD;
        inst.rd = Register::SP;
        inst.rn = Register::SP;
        inst.immediate = static_cast<int32_t>(offset);
        inst.has_immediate = true;
        inst.mnemonic = is_sub ? "SUB" : "ADD";

        std::ostringstream ss;
        ss << "sp, #" << offset;
        inst.operands_str = ss.str();
        return inst;
    }

    // 14. Push / Pop registers (Format 14: 1011x10xx)
    if ((raw & 0xF600) == 0xB400) {
        bool is_pop = (raw & (1 << 11)) != 0;
        bool extra_reg = (raw & (1 << 8)) != 0; // R bit (LR for PUSH, PC for POP)
        uint8_t rlist = static_cast<uint8_t>(raw & 0xFF);

        inst.type = is_pop ? InstructionType::POP : InstructionType::PUSH;
        inst.rn = Register::SP;
        inst.register_list = rlist | (extra_reg ? (1 << (is_pop ? Register::PC : Register::LR)) : 0);
        inst.mnemonic = is_pop ? "POP" : "PUSH";
        inst.operands_str = format_thumb_reg_list(rlist, extra_reg, is_pop);

        if (is_pop && extra_reg) {
            inst.is_branch = true;
            inst.is_return = true;
        }
        return inst;
    }

    // 15. Multiple Load / Store (Format 15: 1100xxxx)
    if ((raw & 0xF000) == 0xC000) {
        bool is_load = (raw & (1 << 11)) != 0;
        uint8_t rb = static_cast<uint8_t>((raw >> 8) & 0x7);
        uint8_t rlist = static_cast<uint8_t>(raw & 0xFF);

        inst.type = is_load ? InstructionType::LDM : InstructionType::STM;
        inst.rn = rb;
        inst.register_list = rlist;
        inst.writeback = true;
        inst.mnemonic = is_load ? "LDMIA" : "STMIA";

        std::ostringstream ss;
        ss << register_name(rb) << "!, " << format_thumb_reg_list(rlist, false, false);
        inst.operands_str = ss.str();
        return inst;
    }

    // 16. Conditional branch / Software interrupt (Format 16: 1101xxxx)
    if ((raw & 0xF000) == 0xD000) {
        uint8_t cond_raw = static_cast<uint8_t>((raw >> 8) & 0xF);
        if (cond_raw == 0xF) {
            // SWI
            uint8_t imm8 = static_cast<uint8_t>(raw & 0xFF);
            inst.type = InstructionType::SWI;
            inst.immediate = imm8;
            inst.has_immediate = true;
            inst.mnemonic = "SWI";
            std::ostringstream ss;
            ss << "#0x" << std::hex << static_cast<int>(imm8);
            inst.operands_str = ss.str();
            return inst;
        } else if (cond_raw < 0xE) {
            // B<cond>
            int32_t imm8 = static_cast<int8_t>(raw & 0xFF);
            int32_t offset = imm8 * 2;
            uint32_t target = address + 4 + offset;

            Condition cond = static_cast<Condition>(cond_raw);
            inst.type = InstructionType::B;
            inst.condition = cond;
            inst.is_branch = true;
            inst.is_conditional_branch = true;
            inst.branch_target = target;
            inst.immediate = offset;
            inst.has_immediate = true;
            inst.mnemonic = "B" + std::string(condition_suffix(cond));

            std::ostringstream ss;
            ss << "0x" << std::hex << std::uppercase << std::setw(8) << std::setfill('0') << target;
            inst.operands_str = ss.str();
            return inst;
        }
    }

    // 17. Unconditional branch (Format 17: 11100xxx)
    if ((raw & 0xF800) == 0xE000) {
        int32_t imm11 = static_cast<int32_t>(raw & 0x7FF);
        if (imm11 & 0x400) {
            imm11 |= 0xFFFFF800; // Sign extend
        }
        int32_t offset = imm11 * 2;
        uint32_t target = address + 4 + offset;

        inst.type = InstructionType::B;
        inst.is_branch = true;
        inst.branch_target = target;
        inst.immediate = offset;
        inst.has_immediate = true;
        inst.mnemonic = "B";

        std::ostringstream ss;
        ss << "0x" << std::hex << std::uppercase << std::setw(8) << std::setfill('0') << target;
        inst.operands_str = ss.str();
        return inst;
    }

    // 19. Long branch with link (Format 19: BL / BLX prefix 11110xxx)
    if ((raw & 0xF800) == 0xF000) {
        int32_t high_offset = static_cast<int32_t>(raw & 0x7FF);
        if (high_offset & 0x400) {
            high_offset |= 0xFFFFF800; // Sign extend 11-bit
        }
        high_offset <<= 12;

        if ((next_raw & 0xE800) == 0xE800) {
            // Suffix found
            bool is_blx = ((next_raw & 0xF800) == 0xE800);
            int32_t low_offset = static_cast<int32_t>(next_raw & 0x7FF) * 2;
            int32_t total_offset = high_offset + low_offset;

            uint32_t target = address + 4 + total_offset;
            if (is_blx) {
                target &= ~3; // Word aligned for BLX to ARM
                inst.type = InstructionType::BLX;
                inst.mnemonic = "BLX";
            } else {
                inst.type = InstructionType::BL;
                inst.mnemonic = "BL";
            }

            inst.raw = (static_cast<uint32_t>(raw) << 16) | next_raw;
            inst.is_branch = true;
            inst.is_call = true;
            inst.branch_target = target;
            inst.immediate = total_offset;
            inst.has_immediate = true;

            std::ostringstream ss;
            ss << "0x" << std::hex << std::uppercase << std::setw(8) << std::setfill('0') << target;
            inst.operands_str = ss.str();
            return inst;
        }

        // Just prefix
        inst.type = InstructionType::BL;
        inst.mnemonic = "BL_PREFIX";
        inst.immediate = high_offset;
        inst.has_immediate = true;
        std::ostringstream ss;
        ss << "#" << high_offset;
        inst.operands_str = ss.str();
        return inst;
    }

    // Unknown instruction fallback
    inst.type = InstructionType::UNKNOWN;
    inst.mnemonic = "DCW";
    std::ostringstream ss;
    ss << "0x" << std::hex << std::uppercase << std::setw(4) << std::setfill('0') << raw;
    inst.operands_str = ss.str();
    return inst;
}

std::vector<ARMInstruction> ThumbDecoder::decode_buffer(std::span<const uint8_t> buffer, uint32_t base_address) {
    std::vector<ARMInstruction> result;
    size_t count = buffer.size() / 2;
    result.reserve(count);

    for (size_t i = 0; i < count; ++i) {
        uint16_t raw = 0;
        std::memcpy(&raw, buffer.data() + (i * 2), 2);
        uint16_t next_raw = 0;
        if (i + 1 < count) {
            std::memcpy(&next_raw, buffer.data() + ((i + 1) * 2), 2);
        }

        uint32_t addr = base_address + static_cast<uint32_t>(i * 2);
        auto inst = decode(raw, addr, next_raw);

        // If it was a combined 32-bit BL instruction, skip the second half
        if ((raw & 0xF800) == 0xF000 && (next_raw & 0xE800) == 0xE800) {
            result.push_back(inst);
            ++i; // Skip suffix
            continue;
        }

        result.push_back(inst);
    }

    return result;
}

} // namespace descomp::arm
