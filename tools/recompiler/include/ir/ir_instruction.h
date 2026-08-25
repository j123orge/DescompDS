#pragma once

#include "ir/ir_value.h"
#include "arm/arm_instruction.h"
#include <cstdint>
#include <string>
#include <vector>
#include <optional>

namespace descomp::ir {

// High level IR operations. These are target-ISA independent.
enum class IROperation : uint16_t {
    UNKNOWN = 0,

    // Data movement
    MOV,        // dest = op0
    MVN,        // dest = ~op0

    // Arithmetic (dest = op0 OP op1 ...)
    ADD,
    ADC,        // add with carry
    SUB,
    SBC,        // subtract with carry
    RSB,        // reverse subtract: dest = op0 - op1
    RSC,        // reverse subtract with carry

    // Logical
    AND,
    ORR,
    EOR,
    BIC,        // and-not

    // Comparison / test (flag-only, no destination written)
    CMP,        // op0 - op1  (sets N,Z,C,V)
    CMN,        // op0 + op1  (sets N,Z,C,V)
    TST,        // op0 & op1  (sets N,Z,C,V)
    TEQ,        // op0 ^ op1  (sets N,Z,C,V)

    // Shifts / rotates (dest = op0 shifted by op1)
    LSL,
    LSR,
    ASR,
    ROR,

    // Multiplication (dest = op0 * op1)
    MUL,

    // Memory
    LOAD,       // dest = *address ;  address in operands[0] (Memory)
    STORE,      // *address = value ; address in dest (Memory), value in operands[0]

    // Control flow
    BRANCH,         // unconditional direct branch to operands[0] (Immediate address)
    COND_BRANCH,    // conditional direct branch; condition + operands[0]
    CALL,           // direct or indirect call (BL / BLX)
    RETURN,         // function return (BX LR / POP {..PC} / LDM {..PC})
    INDIRECT_BRANCH,// branch to register (BX Rm); target in operands[0]

    // Flags
    SET_FLAGS,      // explicit flag materialization (placeholder for future expansion)

    NOP,

    // System / special
    CLZ,            // Count leading zeros: dest = clz(op0)
    MRS,            // Move from PSR: dest = CPSR/SPSR
    MSR,            // Move to PSR: CPSR/SPSR = op0
    SWI,            // Software interrupt / syscall
    BKPT,           // Breakpoint
    MRC,            // Move from coprocessor: dest = P<cp>#op1, CRn, CRm, #op2
    MCR,            // Move to coprocessor: P<cp>#op1, CRn, CRm, #op2 = op0

    // Long multiply (64-bit result)
    UMULL,          // RdLo,RdHi = Rm * Rs
    UMLAL,          // RdLo,RdHi += Rm * Rs
    SMULL,          // RdLo,RdHi = Rm * Rs (signed)
    SMLAL           // RdLo,RdHi += Rm * Rs (signed)
};

// Which CPSR flags an instruction writes. Bit layout matches IRFlag order:
// bit0=N, bit1=Z, bit2=C, bit3=V.
using IRFlagMask = uint8_t;
constexpr IRFlagMask FLAG_N = 1u << 0;
constexpr IRFlagMask FLAG_Z = 1u << 1;
constexpr IRFlagMask FLAG_C = 1u << 2;
constexpr IRFlagMask FLAG_V = 1u << 3;
constexpr IRFlagMask FLAG_ALL = FLAG_N | FLAG_Z | FLAG_C | FLAG_V;

inline bool mask_has_flag(IRFlagMask m, IRFlag f) {
    return (m & (1u << static_cast<uint8_t>(f))) != 0;
}

// A single IR instruction. Provenance (original ARM/Thumb address, raw bytes,
// mode) is preserved so the C++ lifter can reconstruct the original semantics.
struct IRInstruction {
    IROperation op{IROperation::UNKNOWN};

    std::optional<IRValue> dest;     // Destination value (nullopt for flag-only ops / branches)
    std::optional<IRValue> dest2;    // Second destination for long multiply (RdHi)
    std::vector<IRValue> operands;   // Source operands

    std::optional<arm::Condition> condition; // ARM condition under which this executes (nullopt = AL)

    bool update_flags{false};        // Whether this op writes CPSR N/Z/C/V
    IRFlagMask affected_flags{0};    // Which of N/Z/C/V this op writes

    // Control-flow classification (derived from op + original instruction)
    bool is_control_flow{false};
    bool is_terminator{false};       // Block ends here (branch/return/indirect)
    bool is_indirect{false};         // Target is not statically known
    bool is_call{false};             // Call (BL / BLX / indirect call)

    // Provenance
    uint32_t address{0};             // Original ARM/Thumb address
    uint32_t raw{0};                 // Original encoded instruction
    bool is_thumb{false};            // true if decoded from Thumb
    uint8_t size{4};                 // Instruction size in bytes (2 or 4)
    arm::InstructionType original_type{arm::InstructionType::UNKNOWN};
    std::string mnemonic;

    // Preserved register list for block transfers (LDM/STM/PUSH/POP) and
    // any instruction not yet fully lowered. Empty otherwise.
    std::vector<uint8_t> register_list;

    // MRS/MSR: true = SPSR, false = CPSR
    bool spsr{false};
    uint8_t msr_mask{0};

    [[nodiscard]] bool has_dest() const { return dest.has_value(); }

    [[nodiscard]] bool is_branch() const {
        return op == IROperation::BRANCH || op == IROperation::COND_BRANCH ||
               op == IROperation::INDIRECT_BRANCH || op == IROperation::RETURN;
    }

    [[nodiscard]] bool is_direct_branch() const {
        return op == IROperation::BRANCH || op == IROperation::COND_BRANCH;
    }

    [[nodiscard]] std::string to_string() const;
};

} // namespace descomp::ir
