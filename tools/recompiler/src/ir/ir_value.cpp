#include "ir/ir_value.h"
#include <sstream>

using namespace descomp::arm;

namespace descomp::ir {

static std::string ir_shift_name(ShiftType s) {
    switch (s) {
        case ShiftType::LSL: return "LSL";
        case ShiftType::LSR: return "LSR";
        case ShiftType::ASR: return "ASR";
        case ShiftType::ROR: return "ROR";
        case ShiftType::RRX: return "RRX";
    }
    return "?";
}

std::string to_string(const IRValue& v) {
    std::ostringstream ss;
    switch (v.kind) {
        case IRValueKind::Register:
            if (v.id == REG_NONE) ss << "reg(none)";
            else ss << "r" << v.id;
            break;
        case IRValueKind::Flag:
            switch (static_cast<IRFlag>(v.id)) {
                case IRFlag::N: ss << "cpsr_n"; break;
                case IRFlag::Z: ss << "cpsr_z"; break;
                case IRFlag::C: ss << "cpsr_c"; break;
                case IRFlag::V: ss << "cpsr_v"; break;
            }
            break;
        case IRValueKind::Immediate:
            ss << "#0x" << std::hex << std::uppercase << v.immediate;
            break;
        case IRValueKind::Temporary:
            ss << "t" << v.id;
            break;
        case IRValueKind::Memory: {
            const auto& m = v.memory;
            ss << "[r" << static_cast<int>(m.base_reg);
            if (m.displacement != 0) {
                ss << (m.displacement > 0 ? " + 0x" : " - 0x")
                   << std::hex << std::uppercase << (m.displacement > 0 ? m.displacement : -m.displacement);
            }
            if (m.index_reg != REG_NONE) {
                ss << (m.add_index ? " + r" : " - r") << static_cast<int>(m.index_reg);
                if (m.index_shift_amount != 0) {
                    ss << " " << ir_shift_name(m.index_shift) << " 0x"
                       << std::hex << std::uppercase << static_cast<int>(m.index_shift_amount);
                }
            }
            ss << "]";
            if (m.writeback) ss << "!";
            switch (m.access) {
                case IRMemoryAccess::Byte: ss << ".b"; break;
                case IRMemoryAccess::HalfWord: ss << ".h"; break;
                case IRMemoryAccess::Word: ss << ".w"; break;
            }
            if (m.sign_extend) ss << "s";
            break;
        }
    }
    return ss.str();
}

std::ostream& operator<<(std::ostream& os, const IRValue& v) {
    os << to_string(v);
    return os;
}

} // namespace descomp::ir
