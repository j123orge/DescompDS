#include "arm/arm_instruction.h"
#include <sstream>
#include <iomanip>

namespace descomp::arm {

std::string_view condition_suffix(Condition cond) {
    switch (cond) {
        case Condition::EQ: return "EQ";
        case Condition::NE: return "NE";
        case Condition::CS: return "CS";
        case Condition::CC: return "CC";
        case Condition::MI: return "MI";
        case Condition::PL: return "PL";
        case Condition::VS: return "VS";
        case Condition::VC: return "VC";
        case Condition::HI: return "HI";
        case Condition::LS: return "LS";
        case Condition::GE: return "GE";
        case Condition::LT: return "LT";
        case Condition::GT: return "GT";
        case Condition::LE: return "LE";
        case Condition::AL: return "";
        case Condition::NV: return "NV";
        default: return "";
    }
}

std::string_view instruction_type_name(InstructionType type) {
    switch (type) {
        case InstructionType::MOV: return "MOV";
        case InstructionType::MVN: return "MVN";
        case InstructionType::ADD: return "ADD";
        case InstructionType::ADC: return "ADC";
        case InstructionType::SUB: return "SUB";
        case InstructionType::SBC: return "SBC";
        case InstructionType::RSB: return "RSB";
        case InstructionType::RSC: return "RSC";
        case InstructionType::AND: return "AND";
        case InstructionType::ORR: return "ORR";
        case InstructionType::EOR: return "EOR";
        case InstructionType::BIC: return "BIC";
        case InstructionType::CMP: return "CMP";
        case InstructionType::CMN: return "CMN";
        case InstructionType::TST: return "TST";
        case InstructionType::TEQ: return "TEQ";
        case InstructionType::MUL: return "MUL";
        case InstructionType::MLA: return "MLA";
        case InstructionType::UMULL: return "UMULL";
        case InstructionType::UMLAL: return "UMLAL";
        case InstructionType::SMULL: return "SMULL";
        case InstructionType::SMLAL: return "SMLAL";
        case InstructionType::SMLAxy: return "SMLAxy";
        case InstructionType::SMLAWy: return "SMLAWy";
        case InstructionType::SMULWy: return "SMULWy";
        case InstructionType::SMLALxy: return "SMLALxy";
        case InstructionType::LDR: return "LDR";
        case InstructionType::STR: return "STR";
        case InstructionType::LDRB: return "LDRB";
        case InstructionType::STRB: return "STRB";
        case InstructionType::LDRH: return "LDRH";
        case InstructionType::STRH: return "STRH";
        case InstructionType::LDRSB: return "LDRSB";
        case InstructionType::LDRSH: return "LDRSH";
        case InstructionType::LDM: return "LDM";
        case InstructionType::STM: return "STM";
        case InstructionType::PUSH: return "PUSH";
        case InstructionType::POP: return "POP";
        case InstructionType::B: return "B";
        case InstructionType::BL: return "BL";
        case InstructionType::BX: return "BX";
        case InstructionType::BLX: return "BLX";
        case InstructionType::LSL: return "LSL";
        case InstructionType::LSR: return "LSR";
        case InstructionType::ASR: return "ASR";
        case InstructionType::ROR: return "ROR";
        case InstructionType::CLZ: return "CLZ";
        case InstructionType::SWI: return "SWI";
        case InstructionType::BKPT: return "BKPT";
        case InstructionType::MRS: return "MRS";
        case InstructionType::MSR: return "MSR";
        case InstructionType::NOP: return "NOP";
        case InstructionType::QADD: return "QADD";
        case InstructionType::QSUB: return "QSUB";
        case InstructionType::QDADD: return "QDADD";
        case InstructionType::QDSUB: return "QDSUB";
        case InstructionType::MRC: return "MRC";
        case InstructionType::MCR: return "MCR";
        default: return "UNKNOWN";
    }
}

std::string ARMInstruction::to_string() const {
    if (operands_str.empty()) {
        return mnemonic;
    }
    return mnemonic + " " + operands_str;
}

std::string ARMInstruction::to_disasm_line() const {
    std::ostringstream ss;
    ss << std::hex << std::uppercase << std::setw(8) << std::setfill('0') << address << ": ";
    if (is_thumb) {
        ss << std::setw(4) << std::setfill('0') << (raw & 0xFFFF) << "        ";
    } else {
        ss << std::setw(8) << std::setfill('0') << raw << "    ";
    }

    std::string text = to_string();
    ss << text;
    return ss.str();
}

} // namespace descomp::arm
