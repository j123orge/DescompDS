#include "analysis/code_data.h"
#include <set>

namespace descomp::analysis {

const char* CodeDataClassifier::to_string(RegionType t) {
    switch (t) {
        case RegionType::CODE: return "CODE";
        case RegionType::DATA: return "DATA";
        case RegionType::UNKNOWN: return "UNKNOWN";
    }
    return "UNKNOWN";
}

std::map<uint32_t, RegionType> CodeDataClassifier::classify(
    const std::vector<arm::ARMInstruction>& insts,
    const std::map<uint32_t, BasicBlock>& blocks,
    const std::map<uint32_t, Function>& functions,
    uint32_t base_address,
    size_t binary_size)
{
    std::map<uint32_t, RegionType> result;
    std::set<uint32_t> reachable_addrs;
    std::set<uint32_t> branch_targets;
    std::set<uint32_t> literal_pool_targets;

    // Collect reachable addresses from functions' blocks
    for (const auto& [faddr, fn] : functions) {
        for (uint32_t blk_addr : fn.blocks) {
            auto it = blocks.find(blk_addr);
            if (it == blocks.end()) continue;
            for (const auto& inst : it->second.instructions) {
                reachable_addrs.insert(inst.address);
                if (inst.is_branch && inst.branch_target != 0) {
                    branch_targets.insert(inst.branch_target & ~1u);
                }
                // Also collect call targets
                if (inst.is_call && inst.branch_target != 0) {
                    branch_targets.insert(inst.branch_target & ~1u);
                }
            }
        }
        // Also reachable via block successors
        for (uint32_t blk_addr : fn.blocks) {
            auto it = blocks.find(blk_addr);
            if (it == blocks.end()) continue;
            for (uint32_t succ : it->second.successors) branch_targets.insert(succ & ~1u);
        }
    }

    // Collect literal pool targets from PC-relative loads
    // Use both the decoded insts list and the reachable blocks
    auto is_pc_relative_load = [&](const arm::ARMInstruction& inst) -> bool {
        if (inst.type != arm::InstructionType::LDR &&
            inst.type != arm::InstructionType::LDRB &&
            inst.type != arm::InstructionType::LDRH &&
            inst.type != arm::InstructionType::LDRSB &&
            inst.type != arm::InstructionType::LDRSH) return false;
        if (inst.rn != arm::PC) return false;
        // For Thumb, also check Thumb PC-relative (already rn==PC)
        return true;
    };

    for (const auto& inst : insts) {
        if (!is_pc_relative_load(inst)) continue;
        // Check if this load is in a reachable block (otherwise it's not executed)
        if (reachable_addrs.find(inst.address) == reachable_addrs.end()) continue;

        uint32_t target = 0;
        if (inst.is_thumb) {
            // Thumb PC-relative: ((PC+4) & ~3) + offset
            uint32_t pc = (inst.address + 4) & ~3u;
            // inst.immediate is already the offset (positive)
            int32_t off = inst.immediate; // for Thumb, immediate is already scaled (*4 for word)
            // For Thumb, the decoder already computed target for some, but we recompute
            // The immediate in inst is the byte offset (already *4 for word loads)
            target = pc + static_cast<uint32_t>(off);
        } else {
            // ARM PC-relative: PC = address+8, offset is signed immediate
            int32_t off = inst.immediate; // already signed (add_offset applied)
            target = inst.address + 8 + static_cast<uint32_t>(off);
        }
        // Check if target is within binary range and 4-byte aligned
        if (target >= base_address && target < base_address + binary_size && (target & 3) == 0) {
            literal_pool_targets.insert(target);
            // Also mark the next word if the literal is 8-byte? No, just one word.
        }
    }

    // Also collect any other data references: For now, just literal pools
    // Build result for each decoded address
    for (const auto& inst : insts) {
        uint32_t addr = inst.address;
        RegionType rt = RegionType::UNKNOWN;

        bool is_reachable = reachable_addrs.find(addr) != reachable_addrs.end();
        bool is_literal_target = literal_pool_targets.find(addr) != literal_pool_targets.end();
        bool is_branch_target = branch_targets.find(addr) != branch_targets.end();
        bool is_decoded_unknown = (inst.type == arm::InstructionType::UNKNOWN);

        if (is_literal_target) {
            rt = RegionType::DATA;
        } else if (!is_reachable) {
            // Not in any function: likely data or padding
            // If decoded as UNKNOWN, it's DATA; else could be unreached code (still CODE? but we mark UNKNOWN)
            if (is_decoded_unknown) rt = RegionType::DATA;
            else rt = RegionType::UNKNOWN; // unreached valid code, keep UNKNOWN to not inflate CODE
        } else {
            // Reachable
            if (is_decoded_unknown) {
                // If it's a branch target, it's more likely code that we just don't handle (e.g., VFP)
                // Keep as UNKNOWN, not DATA, to indicate unhandled code
                // But if it's surrounded by known code and not a branch target, could be data
                // Heuristic: if not a branch target and previous and next are known, maybe data?
                // For now, keep as UNKNOWN
                rt = RegionType::UNKNOWN;
            } else {
                rt = RegionType::CODE;
            }
        }
        result[addr] = rt;
    }

    // Also ensure literal pool targets that were not in insts (because they were data and decoded as UNKNOWN but still in map)
    // Already handled: they are in result as DATA if they were in insts.
    // But if literal pool target address was not decoded (because we only decoded every 4 bytes, it should be)
    // Ensure any literal target not in result is added as DATA
    for (uint32_t t : literal_pool_targets) {
        if (result.find(t) == result.end()) result[t] = RegionType::DATA;
        else result[t] = RegionType::DATA; // override
    }

    return result;
}

CodeDataStats CodeDataClassifier::analyze(
    const std::vector<arm::ARMInstruction>& insts,
    const std::map<uint32_t, BasicBlock>& blocks,
    const std::map<uint32_t, Function>& functions,
    uint32_t base_address,
    size_t binary_size)
{
    auto m = classify(insts, blocks, functions, base_address, binary_size);
    CodeDataStats s;
    s.total_words = insts.size(); // for ARM, each is 4 bytes
    // Count reachable vs data etc.
    s.reachable_blocks = blocks.size();
    s.reachable_functions = functions.size();
    for (auto& [addr, rt] : m) {
        switch (rt) {
            case RegionType::CODE: s.code_instructions++; break;
            case RegionType::DATA: s.data_words++; break;
            case RegionType::UNKNOWN: s.unknown_words++; break;
        }
    }
    // Also account for words in binary that were not decoded (should be 0 for ARM)
    // For ARM, binary_size/4 should equal insts.size() if decoding every word.
    // If not, the remaining are DATA
    size_t expected_words = binary_size / 4;
    if (expected_words > m.size()) {
        s.data_words += expected_words - m.size();
        s.total_words = expected_words;
    }
    return s;
}

} // namespace descomp::analysis
