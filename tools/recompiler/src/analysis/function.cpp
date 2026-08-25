#include "analysis/function.h"
#include <sstream>
#include <iomanip>
#include <queue>
#include <set>
#include <algorithm>

namespace descomp::analysis {

static std::string hex32(uint32_t val) {
    std::ostringstream ss;
    ss << "0x" << std::hex << std::uppercase << std::setw(8) << std::setfill('0') << val;
    return ss.str();
}

std::string Function::to_json(int indent) const {
    std::string ind(indent, ' ');
    std::ostringstream ss;
    ss << "{\n";
    ss << ind << "\"address\": \"" << hex32(address) << "\",\n";
    ss << ind << "\"name\": \"" << name << "\",\n";
    ss << ind << "\"thumb\": " << (thumb ? "true" : "false") << ",\n";
    ss << ind << "\"size\": " << size << ",\n";
    ss << ind << "\"block_count\": " << blocks.size() << ",\n";
    ss << ind << "\"blocks\": [";
    for (size_t i = 0; i < blocks.size(); ++i) {
        ss << "\"" << hex32(blocks[i]) << "\"" << (i + 1 < blocks.size() ? ", " : "");
    }
    ss << "],\n";
    ss << ind << "\"callees\": [";
    for (size_t i = 0; i < callees.size(); ++i) {
        ss << "\"" << hex32(callees[i]) << "\"" << (i + 1 < callees.size() ? ", " : "");
    }
    ss << "],\n";
    ss << ind << "\"callers\": [";
    for (size_t i = 0; i < callers.size(); ++i) {
        ss << "\"" << hex32(callers[i]) << "\"" << (i + 1 < callers.size() ? ", " : "");
    }
    ss << "],\n";
    ss << ind << "\"indirect_branches\": [";
    for (size_t i = 0; i < indirect_branches.size(); ++i) {
        ss << "\"" << hex32(indirect_branches[i]) << "\"" << (i + 1 < indirect_branches.size() ? ", " : "");
    }
    ss << "],\n";
    ss << ind << "\"indirect_calls\": [";
    for (size_t i = 0; i < indirect_calls.size(); ++i) {
        ss << "\"" << hex32(indirect_calls[i]) << "\"" << (i + 1 < indirect_calls.size() ? ", " : "");
    }
    ss << "],\n";
    ss << ind << "\"unknown_targets\": [";
    for (size_t i = 0; i < unknown_targets.size(); ++i) {
        ss << "\"" << hex32(unknown_targets[i]) << "\"" << (i + 1 < unknown_targets.size() ? ", " : "");
    }
    ss << "]\n";
    ss << "}";
    return ss.str();
}

std::map<uint32_t, Function> FunctionDiscoverer::discover_functions(
    const std::map<uint32_t, BasicBlock>& all_blocks,
    const std::vector<uint32_t>& initial_entry_points,
    bool is_thumb_mode
) {
    std::map<uint32_t, Function> functions;
    if (all_blocks.empty()) {
        return functions;
    }

    std::queue<uint32_t> func_queue;
    std::set<uint32_t> visited_funcs;

    auto enqueue_func = [&](uint32_t addr) {
        // Strip thumb bit (bit 0) if present
        uint32_t clean_addr = addr & ~1U;
        if (visited_funcs.find(clean_addr) == visited_funcs.end()) {
            visited_funcs.insert(clean_addr);
            func_queue.push(clean_addr);
        }
    };

    for (uint32_t ep : initial_entry_points) {
        enqueue_func(ep);
    }

    // Also scan all instructions across all blocks to find all direct BL/BLX targets as seeds
    for (const auto& [start_addr, block] : all_blocks) {
        for (const auto& inst : block.instructions) {
            if (inst.is_call && inst.branch_target != 0) {
                enqueue_func(inst.branch_target);
            }
        }
    }

    // If no entry points were supplied, use the first block as initial seed
    if (visited_funcs.empty() && !all_blocks.empty()) {
        enqueue_func(all_blocks.begin()->first);
    }

    // Process each discovered function
    while (!func_queue.empty()) {
        uint32_t func_entry = func_queue.front();
        func_queue.pop();

        if (all_blocks.find(func_entry) == all_blocks.end()) {
            continue;
        }

        Function fn;
        fn.address = func_entry;
        std::ostringstream name_ss;
        name_ss << "func_" << std::hex << std::uppercase << std::setw(8) << std::setfill('0') << func_entry;
        fn.name = name_ss.str();
        fn.thumb = is_thumb_mode;

        // Traverse basic blocks inside this function
        std::set<uint32_t> fn_blocks;
        std::queue<uint32_t> block_queue;
        block_queue.push(func_entry);
        fn_blocks.insert(func_entry);

        uint32_t min_addr = func_entry;
        uint32_t max_addr = func_entry;

        while (!block_queue.empty()) {
            uint32_t cur_block_addr = block_queue.front();
            block_queue.pop();

            auto it = all_blocks.find(cur_block_addr);
            if (it == all_blocks.end()) continue;

            const BasicBlock& block = it->second;
            min_addr = std::min(min_addr, block.start);
            max_addr = std::max(max_addr, block.end);

            for (const auto& inst : block.instructions) {
                if (inst.is_thumb) {
                    fn.thumb = true;
                }

                if (inst.is_call) {
                    if (inst.is_indirect_branch) {
                        fn.indirect_calls.push_back(inst.address);
                    } else if (inst.branch_target != 0) {
                        uint32_t callee = inst.branch_target & ~1U;
                        if (std::find(fn.callees.begin(), fn.callees.end(), callee) == fn.callees.end()) {
                            fn.callees.push_back(callee);
                        }
                        enqueue_func(callee);
                    }
                } else if (inst.is_indirect_branch && !inst.is_return) {
                    fn.indirect_branches.push_back(inst.address);
                }

                if (inst.is_branch && inst.branch_target != 0 &&
                    all_blocks.find(inst.branch_target & ~1U) == all_blocks.end()) {
                    fn.unknown_targets.push_back(inst.address);
                }
            }

            // Follow successors intra-procedurally (only if not a return block)
            if (!block.is_exit) {
                for (uint32_t succ_addr : block.successors) {
                    if (fn_blocks.find(succ_addr) == fn_blocks.end()) {
                        fn_blocks.insert(succ_addr);
                        block_queue.push(succ_addr);
                    }
                }
            }
        }

        fn.blocks.assign(fn_blocks.begin(), fn_blocks.end());
        std::sort(fn.blocks.begin(), fn.blocks.end());
        fn.size = (max_addr >= min_addr) ? (max_addr - min_addr) : 0;

        functions[func_entry] = std::move(fn);
    }

    // Second pass: Populate callers from callees
    for (const auto& [caller_addr, fn] : functions) {
        for (uint32_t callee_addr : fn.callees) {
            auto it = functions.find(callee_addr);
            if (it != functions.end()) {
                if (std::find(it->second.callers.begin(), it->second.callers.end(), caller_addr) == it->second.callers.end()) {
                    it->second.callers.push_back(caller_addr);
                }
            }
        }
    }

    return functions;
}

} // namespace descomp::analysis
