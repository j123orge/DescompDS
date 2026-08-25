#include "analysis/basic_block.h"
#include <set>
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace descomp::analysis {

static std::string hex32(uint32_t val) {
    std::ostringstream ss;
    ss << "0x" << std::hex << std::uppercase << std::setw(8) << std::setfill('0') << val;
    return ss.str();
}

std::string BasicBlock::to_json(int indent) const {
    std::string ind(indent, ' ');
    std::ostringstream ss;
    ss << "{\n";
    ss << ind << "\"start\": \"" << hex32(start) << "\",\n";
    ss << ind << "\"end\": \"" << hex32(end) << "\",\n";
    ss << ind << "\"instruction_count\": " << instructions.size() << ",\n";
    ss << ind << "\"is_entry\": " << (is_entry ? "true" : "false") << ",\n";
    ss << ind << "\"is_exit\": " << (is_exit ? "true" : "false") << ",\n";
    ss << ind << "\"has_indirect_jump\": " << (has_indirect_jump ? "true" : "false") << ",\n";
    ss << ind << "\"successors\": [";
    for (size_t i = 0; i < successors.size(); ++i) {
        ss << "\"" << hex32(successors[i]) << "\"" << (i + 1 < successors.size() ? ", " : "");
    }
    ss << "],\n";
    ss << ind << "\"predecessors\": [";
    for (size_t i = 0; i < predecessors.size(); ++i) {
        ss << "\"" << hex32(predecessors[i]) << "\"" << (i + 1 < predecessors.size() ? ", " : "");
    }
    ss << "]\n";
    ss << "}";
    return ss.str();
}

std::map<uint32_t, BasicBlock> BasicBlockBuilder::build_blocks(
    std::span<const arm::ARMInstruction> instructions,
    const std::vector<uint32_t>& known_entry_points
) {
    std::map<uint32_t, BasicBlock> blocks;
    if (instructions.empty()) {
        return blocks;
    }

    // Map address to instruction index
    std::map<uint32_t, size_t> addr_to_index;
    for (size_t i = 0; i < instructions.size(); ++i) {
        addr_to_index[instructions[i].address] = i;
    }

    // Step 1: Find leader addresses
    std::set<uint32_t> leaders;
    leaders.insert(instructions.front().address);

    for (uint32_t ep : known_entry_points) {
        if (addr_to_index.find(ep) != addr_to_index.end()) {
            leaders.insert(ep);
        }
    }

    for (size_t i = 0; i < instructions.size(); ++i) {
        const auto& inst = instructions[i];
        uint32_t inst_size = inst.is_thumb ? 2 : 4;
        if (inst.is_thumb && (inst.raw & 0xF8000000) == 0xF0000000) {
            inst_size = 4; // 32-bit Thumb BL
        }
        uint32_t next_addr = inst.address + inst_size;

        if (inst.is_branch) {
            // Target of branch is a leader (if within code range)
            if (inst.branch_target != 0 && addr_to_index.find(inst.branch_target) != addr_to_index.end()) {
                leaders.insert(inst.branch_target);
            }

            // Instruction following branch is a leader
            if (i + 1 < instructions.size()) {
                leaders.insert(next_addr);
            }
        }
    }

    // Step 2: Form basic blocks between leaders
    std::vector<uint32_t> leader_list(leaders.begin(), leaders.end());
    for (size_t l = 0; l < leader_list.size(); ++l) {
        uint32_t cur_leader = leader_list[l];
        auto it = addr_to_index.find(cur_leader);
        if (it == addr_to_index.end()) continue;

        size_t start_idx = it->second;
        BasicBlock block;
        block.start = cur_leader;
        block.is_entry = (std::find(known_entry_points.begin(), known_entry_points.end(), cur_leader) != known_entry_points.end());

        size_t cur_idx = start_idx;
        while (cur_idx < instructions.size()) {
            const auto& inst = instructions[cur_idx];
            block.instructions.push_back(inst);

            uint32_t inst_size = inst.is_thumb ? 2 : 4;
            if (inst.is_thumb && (inst.raw & 0xF8000000) == 0xF0000000) {
                inst_size = 4;
            }
            uint32_t next_addr = inst.address + inst_size;
            block.end = next_addr;

            // Check if this instruction ends the block
            if (inst.is_branch) {
                if (inst.is_return) {
                    block.is_exit = true;
                } else if (inst.is_indirect_branch && !inst.is_call) {
                    block.has_indirect_jump = true;
                    block.is_exit = true;
                }
                break;
            }

            // If next instruction is a leader, stop here
            if (leaders.find(next_addr) != leaders.end()) {
                break;
            }

            ++cur_idx;
        }

        blocks[block.start] = std::move(block);
    }

    // Step 3: Compute successors
    for (auto& [start_addr, block] : blocks) {
        if (block.instructions.empty()) continue;
        const auto& last_inst = block.instructions.back();
        uint32_t inst_size = last_inst.is_thumb ? 2 : 4;
        if (last_inst.is_thumb && (last_inst.raw & 0xF8000000) == 0xF0000000) {
            inst_size = 4;
        }
        uint32_t fallthrough = last_inst.address + inst_size;

if (last_inst.is_conditional_branch) {
            // Successor 1: branch target (if valid and block exists)
            if (blocks.find(last_inst.branch_target) != blocks.end()) {
                block.successors.push_back(last_inst.branch_target);
            }
            // Successor 2: fallthrough - always add the address
            // The interpreter/execution will handle whether a block exists there
            uint32_t ft_addr = last_inst.address + inst_size;
            if (ft_addr > 0) {
                block.successors.push_back(ft_addr);
            }
        } else if (last_inst.type == arm::InstructionType::B) {
            // Unconditional branch: single target
            if (blocks.find(last_inst.branch_target) != blocks.end()) {
                block.successors.push_back(last_inst.branch_target);
            } else {
                block.has_unknown_target = true;
            }
        } else if (last_inst.is_call) {
            // Call (BL): returns to fallthrough instruction
            if (blocks.find(fallthrough) != blocks.end()) {
                block.successors.push_back(fallthrough);
            }
        } else if (last_inst.is_return || (last_inst.is_indirect_branch && !last_inst.is_call)) {
            // Return or indirect jump: no known static successor
            block.is_exit = true;
        } else {
            // Sequential block: fallthrough
            if (blocks.find(fallthrough) != blocks.end()) {
                block.successors.push_back(fallthrough);
            }
        }
    }

    // Step 4: Compute predecessors
    for (const auto& [start_addr, block] : blocks) {
        for (uint32_t succ_addr : block.successors) {
            auto succ_it = blocks.find(succ_addr);
            if (succ_it != blocks.end()) {
                succ_it->second.predecessors.push_back(start_addr);
            }
        }
    }

    return blocks;
}

} // namespace descomp::analysis
