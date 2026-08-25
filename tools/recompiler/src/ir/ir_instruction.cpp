#include "ir/ir_instruction.h"
#include <sstream>

namespace descomp::ir {

static std::string op_name(IROperation op) {
    switch (op) {
        case IROperation::UNKNOWN: return "UNKNOWN";
        case IROperation::MOV: return "MOV";
        case IROperation::MVN: return "MVN";
        case IROperation::ADD: return "ADD";
        case IROperation::ADC: return "ADC";
        case IROperation::SUB: return "SUB";
        case IROperation::SBC: return "SBC";
        case IROperation::RSB: return "RSB";
        case IROperation::RSC: return "RSC";
        case IROperation::AND: return "AND";
        case IROperation::ORR: return "ORR";
        case IROperation::EOR: return "EOR";
        case IROperation::BIC: return "BIC";
        case IROperation::CMP: return "CMP";
        case IROperation::CMN: return "CMN";
        case IROperation::TST: return "TST";
        case IROperation::TEQ: return "TEQ";
        case IROperation::LSL: return "LSL";
        case IROperation::LSR: return "LSR";
        case IROperation::ASR: return "ASR";
        case IROperation::ROR: return "ROR";
        case IROperation::MUL: return "MUL";
        case IROperation::LOAD: return "LOAD";
        case IROperation::STORE: return "STORE";
        case IROperation::BRANCH: return "BRANCH";
        case IROperation::COND_BRANCH: return "COND_BRANCH";
        case IROperation::CALL: return "CALL";
        case IROperation::RETURN: return "RETURN";
        case IROperation::INDIRECT_BRANCH: return "INDIRECT_BRANCH";
        case IROperation::SET_FLAGS: return "SET_FLAGS";
        case IROperation::NOP: return "NOP";
        case IROperation::CLZ: return "CLZ";
        case IROperation::MRS: return "MRS";
        case IROperation::MSR: return "MSR";
        case IROperation::SWI: return "SWI";
        case IROperation::BKPT: return "BKPT";
        case IROperation::MRC: return "MRC";
        case IROperation::MCR: return "MCR";
        case IROperation::UMULL: return "UMULL";
        case IROperation::UMLAL: return "UMLAL";
        case IROperation::SMULL: return "SMULL";
        case IROperation::SMLAL: return "SMLAL";
    }
    return "?";
}

std::string IRInstruction::to_string() const {
    std::ostringstream ss;
    ss << op_name(op);
    if (condition.has_value()) {
        ss << "." << arm::condition_suffix(*condition);
    }
    if (update_flags) ss << "S";

    ss << " ";
    if (dest.has_value()) {
        ss << *dest;
        if (dest2.has_value()) ss << ", " << *dest2;
        ss << " = ";
    }

    for (size_t i = 0; i < operands.size(); ++i) {
        ss << operands[i];
        if (i + 1 < operands.size()) ss << ", ";
    }

    ss << "   ; @" << std::hex << std::uppercase << address
       << (is_thumb ? " (T)" : " (A)");
    return ss.str();
}

} // namespace descomp::ir
