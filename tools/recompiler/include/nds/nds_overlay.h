#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <span>
#include <optional>

namespace descomp::nds {

#pragma pack(push, 1)
struct RawOverlayEntry {
    uint32_t overlay_id;         // Unique Overlay ID
    uint32_t ram_address;        // Load destination address in RAM
    uint32_t ram_size;           // Size in RAM (bytes)
    uint32_t bss_size;           // BSS section size in bytes (zeroed out)
    uint32_t static_init_start;  // Static initialization start function address
    uint32_t static_init_end;    // Static initialization end function address
    uint32_t file_id;            // File ID in FAT table
    uint32_t flags;              // Bit 24: compressed, lower 24 bits: reserved/hash
};
#pragma pack(pop)

static_assert(sizeof(RawOverlayEntry) == 32, "RawOverlayEntry size must be exactly 32 bytes");

struct OverlayInfo {
    uint32_t id{0};
    uint32_t ram_address{0};
    uint32_t ram_size{0};
    uint32_t bss_size{0};
    uint32_t static_init_start{0};
    uint32_t static_init_end{0};
    uint32_t file_id{0};
    bool compressed{false};
    uint32_t rom_offset{0};
    uint32_t rom_size{0};
    std::vector<uint8_t> data;

    [[nodiscard]] std::string to_json(int indent = 2) const;
};

class NDSOverlayParser {
public:
    static std::vector<OverlayInfo> parse_table(
        std::span<const uint8_t> overlay_table_data,
        std::span<const uint8_t> fat_table_data,
        std::span<const uint8_t> rom_data,
        std::string* error_msg = nullptr
    );
};

} // namespace descomp::nds
