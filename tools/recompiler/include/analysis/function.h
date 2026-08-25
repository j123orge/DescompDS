#pragma once

#include "analysis/basic_block.h"
#include <cstdint>
#include <string>
#include <vector>
#include <map>

namespace descomp::analysis {

struct Function {
    uint32_t address{0};
    std::string name;

    bool thumb{false};
    uint32_t size{0};

    std::vector<uint32_t> blocks;
    std::vector<uint32_t> callees;
    std::vector<uint32_t> callers;

    std::vector<uint32_t> indirect_branches;
    std::vector<uint32_t> indirect_calls;
    std::vector<uint32_t> unknown_targets;

    [[nodiscard]] std::string to_json(int indent = 2) const;
};

class FunctionDiscoverer {
public:
    static std::map<uint32_t, Function> discover_functions(
        const std::map<uint32_t, BasicBlock>& all_blocks,
        const std::vector<uint32_t>& initial_entry_points,
        bool is_thumb_mode = false
    );
};

} // namespace descomp::analysis
