#pragma once

#include "ir/ir_block.h"
#include <string>

namespace descomp::lifter {

// High-level lifter: ARM -> IR -> C++ (compilable).
// Wraps CppEmitter and provides convenience entry points plus statistics.
class CppLifter {
public:
    struct Stats {
        size_t total_instructions{0};
        size_t converted{0};
        size_t unknown{0};
    };

    // Lift a single IR function to C++.
    [[nodiscard]] static std::string lift_function(const ir::IRFunction& fn);

    // Lift an entire IR program to a single C++ translation unit.
    [[nodiscard]] static std::string lift_program(const ir::IRProgram& prog);

    // Lift a flat list of IR instructions (utility for tests).
    [[nodiscard]] static std::string lift_instructions(
        const std::vector<ir::IRInstruction>& insts,
        const std::string& func_name = "test_func");

    // Compute conversion statistics for a program.
    [[nodiscard]] static Stats analyze(const ir::IRProgram& prog);
    [[nodiscard]] static Stats analyze_function(const ir::IRFunction& fn);
};

} // namespace descomp::lifter
