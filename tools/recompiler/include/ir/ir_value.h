#pragma once

#include "arm/arm_registers.h"
#include <cstdint>
#include <string>
#include <ostream>

namespace descomp::ir {

// What kind of value an IRValue represents.
enum class IRValueKind : uint8_t {
    Register,    // Physical ARM register (id = register index 0..15)
    Flag,        // CPSR flag (id = IRFlag value)
    Immediate,   // Compile-time constant (immediate field)
    Temporary,   // Compiler-internal SSA temporary (id = temp index)
    Memory       // Memory location described by an IRMemory expression
};

// Individual CPSR flags that can be read/written explicitly.
enum class IRFlag : uint8_t {
    N = 0, // Negative
    Z = 1, // Zero
    C = 2, // Carry
    V = 3  // Overflow
};

enum class IRMemoryAccess : uint8_t {
    Byte = 0,
    HalfWord = 1,
    Word = 2
};

// Memory address expression. Independent of any target ISA.
struct IRMemory {
    uint8_t base_reg{arm::REG_NONE};          // Base register index (Rn)
    int64_t displacement{0};             // Immediate offset (signed)
    uint8_t index_reg{arm::REG_NONE};        // Index register index (Rm), REG_NONE if none
    arm::ShiftType index_shift{arm::ShiftType::LSL};
    uint8_t index_shift_amount{0};      // Shift amount for index register
    bool add_index{true};               // true: base + index, false: base - index
    bool writeback{false};              // Address written back to base register (!)
    bool pre_indexed{true};             // true: [Rn, off]   false: [Rn], off
    IRMemoryAccess access{IRMemoryAccess::Word};
    bool sign_extend{false};            // LDRSB / LDRSH

    [[nodiscard]] bool operator==(const IRMemory& o) const {
        return base_reg == o.base_reg &&
               displacement == o.displacement &&
               index_reg == o.index_reg &&
               index_shift == o.index_shift &&
               index_shift_amount == o.index_shift_amount &&
               add_index == o.add_index &&
               writeback == o.writeback &&
               pre_indexed == o.pre_indexed &&
               access == o.access &&
               sign_extend == o.sign_extend;
    }
};

// A value in the intermediate representation.
struct IRValue {
    IRValueKind kind{IRValueKind::Register};
    uint32_t id{0};            // Register index, IRFlag value, or Temporary id
    int64_t immediate{0};      // Valid when kind == Immediate
    IRMemory memory{};         // Valid when kind == Memory

    static IRValue reg(uint8_t r) {
        IRValue v;
        v.kind = IRValueKind::Register;
        v.id = r;
        return v;
    }

    static IRValue flag(IRFlag f) {
        IRValue v;
        v.kind = IRValueKind::Flag;
        v.id = static_cast<uint32_t>(f);
        return v;
    }

    static IRValue imm(int64_t v) {
        IRValue val;
        val.kind = IRValueKind::Immediate;
        val.immediate = v;
        return val;
    }

    static IRValue temp(uint32_t id) {
        IRValue v;
        v.kind = IRValueKind::Temporary;
        v.id = id;
        return v;
    }

    static IRValue mem(const IRMemory& m) {
        IRValue v;
        v.kind = IRValueKind::Memory;
        v.memory = m;
        return v;
    }

    [[nodiscard]] bool operator==(const IRValue& o) const {
        if (kind != o.kind) return false;
        switch (kind) {
            case IRValueKind::Register:
            case IRValueKind::Flag:
            case IRValueKind::Temporary:
                return id == o.id;
            case IRValueKind::Immediate:
                return immediate == o.immediate;
            case IRValueKind::Memory:
                return memory == o.memory;
        }
        return false;
    }

    [[nodiscard]] bool is_register() const { return kind == IRValueKind::Register; }
    [[nodiscard]] bool is_immediate() const { return kind == IRValueKind::Immediate; }
    [[nodiscard]] bool is_memory() const { return kind == IRValueKind::Memory; }
    [[nodiscard]] bool is_temporary() const { return kind == IRValueKind::Temporary; }
    [[nodiscard]] bool is_flag() const { return kind == IRValueKind::Flag; }
};

std::string to_string(const IRValue& v);

std::ostream& operator<<(std::ostream& os, const IRValue& v);

} // namespace descomp::ir
