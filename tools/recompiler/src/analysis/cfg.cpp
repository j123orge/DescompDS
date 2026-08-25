#include "analysis/cfg.h"
#include <fstream>
#include <sstream>
#include <iomanip>

namespace descomp::analysis {

static std::string hex32(uint32_t val) {
    std::ostringstream ss;
    ss << "0x" << std::hex << std::uppercase << std::setw(8) << std::setfill('0') << val;
    return ss.str();
}

static std::string node_name(uint32_t addr) {
    std::ostringstream ss;
    ss << "block_" << std::hex << std::uppercase << std::setw(8) << std::setfill('0') << addr;
    return ss.str();
}

static std::string escape_dot_string(const std::string& input) {
    std::string output;
    output.reserve(input.size() * 2);
    for (char c : input) {
        if (c == '"') {
            output += "\\\"";
        } else if (c == '\\') {
            output += "\\\\";
        } else if (c == '<') {
            output += "&lt;";
        } else if (c == '>') {
            output += "&gt;";
        } else {
            output.push_back(c);
        }
    }
    return output;
}

CFG::CFG(const Function& fn, const std::map<uint32_t, BasicBlock>& all_blocks)
    : m_function(fn) {
    for (uint32_t block_addr : fn.blocks) {
        auto it = all_blocks.find(block_addr);
        if (it != all_blocks.end()) {
            const BasicBlock& block = it->second;
            m_blocks.push_back(&block);

            if (block.instructions.empty()) continue;
            const auto& last_inst = block.instructions.back();

            if (last_inst.is_conditional_branch) {
                // First successor is branch target (True)
                if (block.successors.size() >= 1) {
                    m_edges.push_back({block.start, block.successors[0], EdgeType::CONDITIONAL_TRUE});
                }
                // Second successor is fallthrough (False)
                if (block.successors.size() >= 2) {
                    m_edges.push_back({block.start, block.successors[1], EdgeType::CONDITIONAL_FALSE});
                }
            } else if (last_inst.type == arm::InstructionType::B) {
                if (!block.successors.empty()) {
                    m_edges.push_back({block.start, block.successors[0], EdgeType::UNCONDITIONAL_JUMP});
                }
            } else if (!block.is_exit) {
                for (uint32_t succ : block.successors) {
                    m_edges.push_back({block.start, succ, EdgeType::SEQUENTIAL});
                }
            }
        }
    }
}

std::string CFG::export_to_dot() const {
    std::ostringstream ss;
    ss << "digraph \"" << m_function.name << "\" {\n";
    ss << "  graph [fontname=\"Courier New\", fontsize=10, rankdir=TB];\n";
    ss << "  node  [fontname=\"Courier New\", fontsize=9, shape=box, style=\"filled,rounded\", fillcolor=\"#F8F9FA\", color=\"#495057\"];\n";
    ss << "  edge  [fontname=\"Courier New\", fontsize=8];\n\n";

    // Nodes
    for (const BasicBlock* block : m_blocks) {
        std::string name = node_name(block->start);
        std::ostringstream label_ss;
        label_ss << "=== " << hex32(block->start) << " .. " << hex32(block->end) << " ===\\l";

        for (const auto& inst : block->instructions) {
            std::string line = inst.to_disasm_line();
            label_ss << escape_dot_string(line) << "\\l";
        }

        std::string fillcolor = "#F8F9FA";
        std::string bordercolor = "#495057";

        if (block->start == m_function.address) {
            fillcolor = "#E3F2FD"; // Light blue for entry
            bordercolor = "#1976D2";
        } else if (block->is_exit) {
            fillcolor = "#FFEBEE"; // Light red for exit
            bordercolor = "#D32F2F";
        }

        ss << "  " << name << " [\n";
        ss << "    label=\"" << label_ss.str() << "\",\n";
        ss << "    fillcolor=\"" << fillcolor << "\",\n";
        ss << "    color=\"" << bordercolor << "\"\n";
        ss << "  ];\n";
    }

    ss << "\n";

    // Edges
    for (const auto& edge : m_edges) {
        std::string from_name = node_name(edge.from_block);
        std::string to_name = node_name(edge.to_block);

        std::string color = "#000000";
        std::string label;

        switch (edge.type) {
            case EdgeType::CONDITIONAL_TRUE:
                color = "#2E7D32"; // Green
                label = "true";
                break;
            case EdgeType::CONDITIONAL_FALSE:
                color = "#C62828"; // Red
                label = "false";
                break;
            case EdgeType::UNCONDITIONAL_JUMP:
                color = "#1565C0"; // Blue
                label = "jump";
                break;
            case EdgeType::CALL:
                color = "#6A1B9A"; // Purple
                label = "call";
                break;
            default:
                color = "#37474F";
                break;
        }

        ss << "  " << from_name << " -> " << to_name << " [color=\"" << color << "\"";
        if (!label.empty()) {
            ss << ", label=\"" << label << "\", fontcolor=\"" << color << "\"";
        }
        ss << "];\n";
    }

    ss << "}\n";
    return ss.str();
}

bool CFG::save_to_file(const std::filesystem::path& path) const {
    std::ofstream out(path);
    if (!out.is_open()) return false;
    out << export_to_dot();
    return out.good();
}

bool CFG::export_all(
    const std::filesystem::path& output_dir,
    const std::map<uint32_t, Function>& functions,
    const std::map<uint32_t, BasicBlock>& all_blocks,
    std::string* error_msg
) {
    std::error_code ec;
    std::filesystem::create_directories(output_dir, ec);
    if (ec) {
        if (error_msg) *error_msg = "Failed to create CFG output directory: " + output_dir.string();
        return false;
    }

    for (const auto& [addr, fn] : functions) {
        CFG cfg(fn, all_blocks);
        std::ostringstream filename;
        filename << "function_" << std::hex << std::uppercase << std::setw(8) << std::setfill('0') << addr << ".dot";
        std::filesystem::path file_path = output_dir / filename.str();
        if (!cfg.save_to_file(file_path)) {
            if (error_msg) *error_msg = "Failed to write CFG file: " + file_path.string();
            return false;
        }
    }

    return true;
}

} // namespace descomp::analysis
