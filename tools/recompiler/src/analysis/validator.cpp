#include "analysis/validator.h"
#include "arm/arm_decoder.h"
#include "arm/thumb_decoder.h"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <fstream>
#include <iostream>

namespace descomp::analysis {

static std::string hex32(uint32_t val) {
    std::ostringstream ss;
    ss << "0x" << std::hex << std::uppercase << std::setw(8) << std::setfill('0') << val;
    return ss.str();
}

std::string ValidationIssue::to_json() const {
    std::string sev_str;
    switch (severity) {
        case Severity::INFO: sev_str = "INFO"; break;
        case Severity::WARNING: sev_str = "WARNING"; break;
        case Severity::ERROR: sev_str = "ERROR"; break;
    }
    std::ostringstream ss;
    ss << "{\n";
    ss << "      \"severity\": \"" << sev_str << "\",\n";
    ss << "      \"category\": \"" << category << "\",\n";
    ss << "      \"address\": \"" << hex32(address) << "\",\n";
    ss << "      \"message\": \"" << message << "\"\n";
    ss << "    }";
    return ss.str();
}

bool Phase1Validator::is_valid_executable_address(
    uint32_t addr,
    const nds::ParsedHeader& header,
    const std::vector<nds::OverlayInfo>& overlays
) {
    // Strip thumb bit (bit 0)
    addr &= ~1U;

    // 1. Check ARM9 main binary RAM range
    if (addr >= header.arm9_ram_address && addr < (header.arm9_ram_address + header.arm9_size)) {
        return true;
    }

    // 2. Check Overlays RAM ranges
    for (const auto& ov : overlays) {
        if (addr >= ov.ram_address && addr < (ov.ram_address + ov.ram_size)) {
            return true;
        }
    }

    // 3. Check ARM9 ITCM / DTCM / BIOS / General RAM
    if (addr < 0x00008000) { // ITCM (32 KB)
        return true;
    }
    if (addr >= 0x02000000 && addr <= 0x023FFFFF) { // 4MB Main RAM
        return true;
    }
    if (addr >= 0xFFFF0000 && addr <= 0xFFFF7FFF) { // ARM9 BIOS
        return true;
    }

    return false;
}

CompleteValidationReport Phase1Validator::validate_rom(const nds::NDSImage& rom) {
    CompleteValidationReport report;
    auto& stats = report.global_stats;
    const auto& header = rom.header();
    const auto& overlays = rom.arm9_overlays();

    // -------------------------------------------------------------
    // Step 1: Decode and Validate ARM9 Main Binary
    // -------------------------------------------------------------
    auto arm9_bin = rom.arm9_binary();
    std::vector<arm::ARMInstruction> arm9_instructions;

    if (!arm9_bin.empty()) {
        arm9_instructions = arm::ARMDecoder::decode_buffer(arm9_bin, header.arm9_ram_address);
        stats.arm9_instructions += arm9_instructions.size();

        for (const auto& inst : arm9_instructions) {
            // Count instruction classifications
            if (inst.type == arm::InstructionType::UNKNOWN) {
                stats.unknown_instructions++;
            }
            if (inst.is_branch) {
                stats.branches++;
                if (inst.is_conditional_branch) {
                    stats.conditional_branches++;
                }
            }
            if (inst.is_call) {
                stats.calls++;
                if (inst.is_indirect_branch) {
                    stats.indirect_calls++;
                }
            } else if (inst.is_indirect_branch && !inst.is_return) {
                stats.indirect_branches++;
            }

            if (inst.is_return) {
                stats.returns++;
            }

            // Check ARM -> Thumb mode transitions
            if (inst.type == arm::InstructionType::BLX && !inst.is_indirect_branch) {
                stats.arm_to_thumb_transitions++;
            }

            // Check branch targets validity
            if (inst.is_branch && inst.branch_target != 0) {
                if (!is_valid_executable_address(inst.branch_target, header, overlays)) {
                    stats.invalid_targets++;
                    stats.validation_errors.push_back({
                        ValidationIssue::Severity::WARNING,
                        "INVALID_BRANCH_TARGET",
                        inst.address,
                        "Branch target " + hex32(inst.branch_target) + " points outside known executable memory."
                    });
                }
            }
        }
    }

    // -------------------------------------------------------------
    // Step 2: Build Basic Blocks and Validate
    // -------------------------------------------------------------
    std::vector<uint32_t> entry_points = { header.arm9_entry_address };
    for (const auto& ov : overlays) {
        if (ov.static_init_start != 0) {
            entry_points.push_back(ov.static_init_start);
        }
    }

    auto basic_blocks = BasicBlockBuilder::build_blocks(arm9_instructions, entry_points);
    stats.basic_blocks += basic_blocks.size();

    for (const auto& [start_addr, block] : basic_blocks) {
        // Validation check: block bounds
        if (block.start < header.arm9_ram_address || block.end > (header.arm9_ram_address + header.arm9_size)) {
            stats.invalid_blocks++;
            stats.validation_errors.push_back({
                ValidationIssue::Severity::ERROR,
                "INVALID_BLOCK_BOUNDS",
                block.start,
                "Basic block bounds (" + hex32(block.start) + " .. " + hex32(block.end) + ") exceed binary range."
            });
        }

        // Validation check: block successors
        for (uint32_t succ_addr : block.successors) {
            if (basic_blocks.find(succ_addr) == basic_blocks.end()) {
                stats.invalid_targets++;
                stats.validation_errors.push_back({
                    ValidationIssue::Severity::WARNING,
                    "UNRESOLVED_BLOCK_SUCCESSOR",
                    block.start,
                    "Basic block has successor " + hex32(succ_addr) + " that is not in the block table."
                });
            }
        }

        // Validation check: conditional branches should have 2 paths when applicable
        if (!block.instructions.empty()) {
            const auto& last_inst = block.instructions.back();
            if (last_inst.is_conditional_branch) {
                if (block.successors.size() < 2) {
                    // Check if one of the paths was out of bounds
                    stats.validation_errors.push_back({
                        ValidationIssue::Severity::INFO,
                        "CONDITIONAL_BRANCH_SINGLE_TARGET",
                        last_inst.address,
                        "Conditional branch at " + hex32(last_inst.address) + " has only 1 valid in-binary successor."
                    });
                }
            } else if (last_inst.is_return) {
                if (!block.successors.empty()) {
                    stats.validation_errors.push_back({
                        ValidationIssue::Severity::ERROR,
                        "RETURN_BLOCK_HAS_SUCCESSOR",
                        block.start,
                        "Return block has unexpected successors."
                    });
                }
            }
        }
    }

    // -------------------------------------------------------------
    // Step 3: Discover Functions and Validate
    // -------------------------------------------------------------
    auto functions = FunctionDiscoverer::discover_functions(basic_blocks, entry_points);
    stats.functions += functions.size();

    std::set<uint32_t> seen_func_addrs;
    for (const auto& [func_addr, fn] : functions) {
        // Check duplicates
        if (seen_func_addrs.find(func_addr) != seen_func_addrs.end()) {
            stats.duplicate_functions++;
            stats.validation_errors.push_back({
                ValidationIssue::Severity::ERROR,
                "DUPLICATE_FUNCTION",
                func_addr,
                "Duplicate function address detected: " + hex32(func_addr)
            });
        }
        seen_func_addrs.insert(func_addr);

        // Check valid entry address
        if (!is_valid_executable_address(func_addr, header, overlays)) {
            stats.validation_errors.push_back({
                ValidationIssue::Severity::ERROR,
                "INVALID_FUNCTION_ADDRESS",
                func_addr,
                "Function address " + hex32(func_addr) + " is not in valid executable memory."
            });
        }

        // Check entry block presence
        if (basic_blocks.find(func_addr) == basic_blocks.end()) {
            stats.functions_without_entry++;
            stats.validation_errors.push_back({
                ValidationIssue::Severity::ERROR,
                "FUNCTION_WITHOUT_ENTRY_BLOCK",
                func_addr,
                "Function at " + hex32(func_addr) + " does not have a corresponding entry basic block."
            });
        }
    }

    // -------------------------------------------------------------
    // Step 4: Validate all 103 ARM9 Overlays
    // -------------------------------------------------------------
    report.overlay_reports.reserve(overlays.size());

    for (const auto& ov : overlays) {
        OverlayAnalysisResult ov_res;
        ov_res.overlay_id = ov.id;
        ov_res.ram_address = ov.ram_address;
        ov_res.ram_size = ov.ram_size;
        ov_res.compressed = ov.compressed;

        if (!ov.data.empty() && !ov.compressed) {
            // Uncompressed overlay: disassemble and analyze
            auto ov_insts = arm::ARMDecoder::decode_buffer(ov.data, ov.ram_address);
            ov_res.instruction_count = ov_insts.size();
            stats.arm9_instructions += ov_insts.size();

            for (const auto& inst : ov_insts) {
                if (inst.type == arm::InstructionType::UNKNOWN) {
                    ov_res.unknown_count++;
                    stats.unknown_instructions++;
                }
                if (inst.is_branch) {
                    stats.branches++;
                    if (inst.is_conditional_branch) stats.conditional_branches++;
                    if (inst.branch_target != 0 && !is_valid_executable_address(inst.branch_target, header, overlays)) {
                        ov_res.invalid_targets++;
                        stats.invalid_targets++;
                    }
                }
                if (inst.is_call) {
                    stats.calls++;
                    if (inst.is_indirect_branch) stats.indirect_calls++;
                } else if (inst.is_indirect_branch && !inst.is_return) {
                    stats.indirect_branches++;
                }
                if (inst.is_return) stats.returns++;
            }

            std::vector<uint32_t> ov_eps;
            if (ov.static_init_start != 0) ov_eps.push_back(ov.static_init_start);

            auto ov_blocks = BasicBlockBuilder::build_blocks(ov_insts, ov_eps);
            ov_res.basic_block_count = ov_blocks.size();
            stats.basic_blocks += ov_blocks.size();

            auto ov_funcs = FunctionDiscoverer::discover_functions(ov_blocks, ov_eps);
            ov_res.function_count = ov_funcs.size();
            stats.functions += ov_funcs.size();
        } else if (ov.compressed) {
            // Compressed overlay note
            ov_res.instruction_count = 0;
        }

        report.overlay_reports.push_back(ov_res);
    }

    return report;
}

std::string CompleteValidationReport::to_json(int indent) const {
    std::string ind(indent, ' ');
    std::ostringstream ss;
    ss << "{\n";
    ss << ind << "\"arm9_instructions\": " << global_stats.arm9_instructions << ",\n";
    ss << ind << "\"thumb_instructions\": " << global_stats.thumb_instructions << ",\n";
    ss << ind << "\"unknown_instructions\": " << global_stats.unknown_instructions << ",\n";
    ss << ind << "\"branches\": " << global_stats.branches << ",\n";
    ss << ind << "\"conditional_branches\": " << global_stats.conditional_branches << ",\n";
    ss << ind << "\"calls\": " << global_stats.calls << ",\n";
    ss << ind << "\"returns\": " << global_stats.returns << ",\n";
    ss << ind << "\"indirect_branches\": " << global_stats.indirect_branches << ",\n";
    ss << ind << "\"indirect_calls\": " << global_stats.indirect_calls << ",\n";
    ss << ind << "\"arm_to_thumb_transitions\": " << global_stats.arm_to_thumb_transitions << ",\n";
    ss << ind << "\"thumb_to_arm_transitions\": " << global_stats.thumb_to_arm_transitions << ",\n";
    ss << ind << "\"functions\": " << global_stats.functions << ",\n";
    ss << ind << "\"basic_blocks\": " << global_stats.basic_blocks << ",\n";
    ss << ind << "\"invalid_targets\": " << global_stats.invalid_targets << ",\n";
    ss << ind << "\"invalid_blocks\": " << global_stats.invalid_blocks << ",\n";
    ss << ind << "\"duplicate_functions\": " << global_stats.duplicate_functions << ",\n";
    ss << ind << "\"overlapping_functions\": " << global_stats.overlapping_functions << ",\n";
    ss << ind << "\"functions_without_entry\": " << global_stats.functions_without_entry << ",\n";
    ss << ind << "\"total_overlays_analyzed\": " << overlay_reports.size() << ",\n";
    ss << ind << "\"validation_errors\": [\n";

    for (size_t i = 0; i < global_stats.validation_errors.size(); ++i) {
        ss << ind << "  " << global_stats.validation_errors[i].to_json()
           << (i + 1 < global_stats.validation_errors.size() ? "," : "") << "\n";
    }

    ss << ind << "]\n";
    ss << "}\n";
    return ss.str();
}

std::string CompleteValidationReport::summary() const {
    std::ostringstream ss;
    ss << "=== DescompDS Phase 1.5 Validation Report ===\n";
    ss << "ARM9 Instructions Decoded:   " << global_stats.arm9_instructions << "\n";
    ss << "Thumb Instructions Decoded:  " << global_stats.thumb_instructions << "\n";
    ss << "Unknown Instructions:        " << global_stats.unknown_instructions << "\n";
    ss << "Branches (Total):            " << global_stats.branches << " (Conditional: " << global_stats.conditional_branches << ")\n";
    ss << "Calls (BL / BLX):            " << global_stats.calls << " (Indirect: " << global_stats.indirect_calls << ")\n";
    ss << "Returns (BX LR / POP PC):    " << global_stats.returns << "\n";
    ss << "Indirect Branches (BX Rm):   " << global_stats.indirect_branches << "\n";
    ss << "ARM -> Thumb Transitions:    " << global_stats.arm_to_thumb_transitions << "\n";
    ss << "Discovered Functions:        " << global_stats.functions << "\n";
    ss << "Identified Basic Blocks:     " << global_stats.basic_blocks << "\n";
    ss << "Total Overlays Processed:    " << overlay_reports.size() << "\n";
    ss << "Invalid Targets Detected:    " << global_stats.invalid_targets << "\n";
    ss << "Invalid Blocks:              " << global_stats.invalid_blocks << "\n";
    ss << "Duplicate Functions:         " << global_stats.duplicate_functions << "\n";
    ss << "Validation Issues Logged:    " << global_stats.validation_errors.size() << "\n";
    return ss.str();
}

bool CompleteValidationReport::save_to_file(const std::filesystem::path& path) const {
    std::ofstream out(path);
    if (!out.is_open()) return false;
    out << to_json(2);
    return out.good();
}

} // namespace descomp::analysis
