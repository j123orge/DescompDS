#include "lifter/cpp_lifter.h"
#include "lifter/cpp_emitter.h"

namespace descomp::lifter {

std::string CppLifter::lift_function(const ir::IRFunction& fn) {
    CppEmitter emitter;
    emitter.emit_prelude();
    emitter.emit_function(fn);
    return emitter.str();
}

std::string CppLifter::lift_program(const ir::IRProgram& prog) {
    CppEmitter emitter;
    emitter.emit_program(prog);
    return emitter.str();
}

std::string CppLifter::lift_instructions(
    const std::vector<ir::IRInstruction>& insts,
    const std::string& func_name)
{
    // Build a synthetic function containing one block with the given insts.
    ir::IRFunction fn;
    fn.address = insts.empty() ? 0 : insts.front().address;
    fn.name = func_name;
    fn.thumb = insts.empty() ? false : insts.front().is_thumb;
    fn.blocks = { fn.address };

    ir::IRBasicBlock block;
    block.start = fn.address;
    block.end = fn.address;
    block.is_thumb = fn.thumb;
    block.instructions = insts;
    if (!insts.empty()) {
        // Compute end as last inst address + size
        const auto& last = insts.back();
        block.end = last.address + last.size;
    }
    fn.block_map[fn.address] = std::move(block);

    return lift_function(fn);
}

CppLifter::Stats CppLifter::analyze(const ir::IRProgram& prog) {
    Stats s;
    for (auto& [addr, fn] : prog.functions) {
        auto fs = analyze_function(fn);
        s.total_instructions += fs.total_instructions;
        s.converted += fs.converted;
        s.unknown += fs.unknown;
    }
    return s;
}

CppLifter::Stats CppLifter::analyze_function(const ir::IRFunction& fn) {
    Stats s;
    for (auto& [addr, block] : fn.block_map) {
        for (auto& inst : block.instructions) {
            ++s.total_instructions;
            if (inst.op == ir::IROperation::UNKNOWN) ++s.unknown;
            else ++s.converted;
        }
    }
    return s;
}

} // namespace descomp::lifter
