#pragma once

#include "nds/nds_image.h"
#include "arm/arm_instruction.h"
#include "analysis/basic_block.h"
#include "analysis/function.h"
#include "analysis/cfg.h"

#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <filesystem>

namespace descomp::analysis {

struct ValidationIssue {
    enum class Severity {
        INFO,
        WARNING,
        ERROR
    };

    Severity severity{Severity::INFO};
    std::string category;
    uint32_t address{0};
    std::string message;

    [[nodiscard]] std::string to_json() const;
};

struct AnalysisStats {
    uint64_t arm9_instructions{0};
    uint64_t thumb_instructions{0};
    uint64_t unknown_instructions{0};

    uint64_t branches{0};
    uint64_t conditional_branches{0};
    uint64_t calls{0};
    uint64_t returns{0};
    uint64_t indirect_branches{0};
    uint64_t indirect_calls{0};

    uint64_t arm_to_thumb_transitions{0};
    uint64_t thumb_to_arm_transitions{0};

    uint64_t functions{0};
    uint64_t basic_blocks{0};

    uint64_t invalid_targets{0};
    uint64_t invalid_blocks{0};
    uint64_t duplicate_functions{0};
    uint64_t overlapping_functions{0};
    uint64_t functions_without_entry{0};

    std::vector<ValidationIssue> validation_errors;
};

struct OverlayAnalysisResult {
    uint32_t overlay_id{0};
    uint32_t ram_address{0};
    uint32_t ram_size{0};
    bool compressed{false};
    uint64_t instruction_count{0};
    uint64_t basic_block_count{0};
    uint64_t function_count{0};
    uint64_t unknown_count{0};
    uint64_t invalid_targets{0};
};

struct CompleteValidationReport {
    AnalysisStats global_stats;
    std::vector<OverlayAnalysisResult> overlay_reports;

    [[nodiscard]] std::string to_json(int indent = 2) const;
    [[nodiscard]] std::string summary() const;
    [[nodiscard]] bool save_to_file(const std::filesystem::path& path) const;
};

class Phase1Validator {
public:
    static CompleteValidationReport validate_rom(const nds::NDSImage& rom);

private:
    static bool is_valid_executable_address(
        uint32_t addr,
        const nds::ParsedHeader& header,
        const std::vector<nds::OverlayInfo>& overlays
    );
};

} // namespace descomp::analysis
