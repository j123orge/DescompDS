#pragma once

#include "analysis/basic_block.h"
#include "analysis/function.h"
#include <filesystem>
#include <string>
#include <vector>
#include <map>

namespace descomp::analysis {

enum class EdgeType {
    SEQUENTIAL,
    CONDITIONAL_TRUE,
    CONDITIONAL_FALSE,
    UNCONDITIONAL_JUMP,
    CALL,
    INDIRECT
};

struct CFGEdge {
    uint32_t from_block{0};
    uint32_t to_block{0};
    EdgeType type{EdgeType::SEQUENTIAL};
};

class CFG {
public:
    CFG(const Function& fn, const std::map<uint32_t, BasicBlock>& all_blocks);

    [[nodiscard]] const Function& function() const { return m_function; }
    [[nodiscard]] const std::vector<CFGEdge>& edges() const { return m_edges; }
    [[nodiscard]] const std::vector<const BasicBlock*>& blocks() const { return m_blocks; }

    [[nodiscard]] std::string export_to_dot() const;
    [[nodiscard]] bool save_to_file(const std::filesystem::path& path) const;

    static bool export_all(
        const std::filesystem::path& output_dir,
        const std::map<uint32_t, Function>& functions,
        const std::map<uint32_t, BasicBlock>& all_blocks,
        std::string* error_msg = nullptr
    );

private:
    Function m_function;
    std::vector<const BasicBlock*> m_blocks;
    std::vector<CFGEdge> m_edges;
};

} // namespace descomp::analysis
