#pragma once

#include "arm/arm_registers.h"
#include <cstdint>
#include <string>
#include <string_view>

namespace descomp::arm {

enum class Condition : uint8_t {
    EQ = 0x0, // Equal (Z set)
    NE = 0x1, // Not Equal (Z clear)
    CS = 0x2, // Carry Set / Unsigned Higher or Same (C set)
    HS = 0x2, // Synonym for CS
    CC = 0x3, // Carry Clear / Unsigned Lower (C clear)
    LO = 0x3, // Synonym for CC
    MI = 0x4, // Minus / Negative (N set)
    PL = 0x5, // Plus / Positive or Zero (N clear)
    VS = 0x6, // Overflow (V set)
    VC = 0x7, // No Overflow (V clear)
    HI = 0x8, // Unsigned Higher (C set and Z clear)
    LS = 0x9, // Unsigned Lower or Same (C clear or Z set)
    GE = 0xA, // Signed Greater Than or Equal (N equals V)
    LT = 0xB, // Signed Less Than (N not equal to V)
    GT = 0xC, // Signed Greater Than (Z clear and N equals V)
    LE = 0xD, // Signed Less Than or Equal (Z set or N not equal to V)
    AL = 0xE, // Always (unconditional)
    NV = 0xF  // Never / Unconditional BLX (ARMv5)
};

enum class InstructionType : uint16_t {
    UNKNOWN = 0,
    // Data Processing
    MOV,
    MVN,
    ADD,
    ADC,
    SUB,
    SBC,
    RSB,
    RSC,
    AND,
    ORR,
    EOR,
    BIC,
    CMP,
    CMN,
    TST,
    TEQ,

    // Multiply
    MUL,
    MLA,
    UMULL,
    UMLAL,
    SMULL,
    SMLAL,
    SMLAxy,
    SMLAWy,
    SMULWy,
    SMLALxy,

    // Single Data Transfer
    LDR,
    STR,
    LDRB,
    STRB,
    LDRH,
    STRH,
    LDRSB,
    LDRSH,

    // Block Transfer / Stack
    LDM,
    STM,
    PUSH,
    POP,

    // Branching
    B,
    BL,
    BX,
    BLX,

    // Shifts
    LSL,
    LSR,
    ASR,
    ROR,

    // Count Leading Zeros & Misc
    CLZ,
    SWI,
    BKPT,
    MRS,
    MSR,
    NOP,
    QADD,
    QSUB,
    QDADD,
    QDSUB,

    // Coprocessor register transfer
    MRC,
    MCR
};

struct ARMInstruction {
    uint32_t address{0};
    uint32_t raw{0};
    InstructionType type{InstructionType::UNKNOWN};
    Condition condition{Condition::AL};

    uint8_t rd{REG_NONE};
    uint8_t rn{REG_NONE};
    uint8_t rm{REG_NONE};
    uint8_t rs{REG_NONE};

    int32_t immediate{0};
    bool has_immediate{false};

    ShiftType shift_type{ShiftType::LSL};
    uint8_t shift_amount{0};
    uint8_t shift_reg{REG_NONE};
    bool has_shift{false};
    bool has_reg_shift{false};

    uint16_t register_list{0}; // Bitmask for LDM/STM/PUSH/POP

    bool sets_flags{false};    // S bit
    bool is_thumb{false};      // 16-bit Thumb instruction

    bool writeback{false};     // ! bit (W)
    bool pre_indexed{true};    // P bit
    bool add_offset{true};     // U bit (true = add, false = subtract)

    // MRS/MSR: true = SPSR, false = CPSR
    bool spsr{false};
    uint8_t msr_mask{0};       // Field mask for MSR (bits 19-16, 0x8=f,0x4=s,0x2=x,0x1=c)

    // Coprocessor fields (MRC/MCR)
    uint8_t cp_num{15};        // Coprocessor number (bits 11:8)
    uint8_t crn{0};            // Coprocessor register N (bits 19:16)
    uint8_t crm{0};            // Coprocessor register M (bits 3:0)
    uint8_t cp_op1{0};        // Coprocessor opcode1 (bits 23:21 for MCR, bits 23:20 for MRC)
    uint8_t cp_op2{0};        // Coprocessor opcode2 (bits 7:5)

    // Flow control analysis properties
    bool is_branch{false};
    bool is_conditional_branch{false};
    bool is_call{false};
    bool is_return{false};
    bool is_indirect_branch{false};
    uint32_t branch_target{0};

    std::string mnemonic;
    std::string operands_str;

    [[nodiscard]] std::string to_string() const;
    [[nodiscard]] std::string to_disasm_line() const;
};

std::string_view condition_suffix(Condition cond);
std::string_view instruction_type_name(InstructionType type);

} // namespace descomp::arm
