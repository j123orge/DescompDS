#pragma once

#include "arm/arm_instruction.h"
#include "analysis/basic_block.h"
#include "analysis/function.h"

#include <cstdint>
#include <map>
#include <vector>

namespace descomp::analysis {

enum class RegionType : uint8_t {
    CODE = 0,   // Decoded as valid ARM/Thumb and reachable
    DATA = 1,   // Word is data (literal pool, not executed)
    UNKNOWN = 2 // Could not be classified or decoded as UNKNOWN
};

struct CodeDataStats {
    size_t total_words{0};      // Total 4-byte words in binary
    size_t code_instructions{0}; // CODE count (ARM 4-byte or Thumb 2-byte)
    size_t data_words{0};        // DATA count
    size_t unknown_words{0};     // UNKNOWN count
    size_t reachable_blocks{0};
    size_t reachable_functions{0};
};

class CodeDataClassifier {
public:
    // Classify every 4-byte word address in the decoded range.
    // For ARM, each word is 4 bytes; for Thumb, 2 bytes but we still track 4-byte words for stats.
    static std::map<uint32_t, RegionType> classify(
        const std::vector<arm::ARMInstruction>& insts,
        const std::map<uint32_t, BasicBlock>& blocks,
        const std::map<uint32_t, Function>& functions,
        uint32_t base_address,
        size_t binary_size);

    // Convenience that also returns stats.
    static CodeDataStats analyze(
        const std::vector<arm::ARMInstruction>& insts,
        const std::map<uint32_t, BasicBlock>& blocks,
        const std::map<uint32_t, Function>& functions,
        uint32_t base_address,
        size_t binary_size);

    static const char* to_string(RegionType t);
};

} // namespace descomp::analysis
