#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <span>
#include <optional>

namespace descomp::nds {

struct NitroFile {
    uint32_t file_id{0};
    std::string path;
    uint32_t rom_offset{0};
    uint32_t size{0};
};

struct NitroFSInfo {
    std::vector<NitroFile> files;
    uint32_t total_directories{0};
    uint32_t total_files{0};

    [[nodiscard]] std::string to_json(int indent = 2) const;
};

class NitroFSParser {
public:
    static NitroFSInfo parse(
        std::span<const uint8_t> fnt_data,
        std::span<const uint8_t> fat_data,
        std::string* error_msg = nullptr
    );
};

} // namespace descomp::nds
