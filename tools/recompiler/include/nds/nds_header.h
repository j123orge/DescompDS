#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <span>
#include <optional>

namespace descomp::nds {

#pragma pack(push, 1)
struct RawNDSHeader {
    char game_title[12];            // 0x000: Game Title (ASCII, uppercase, null-padded)
    char game_code[4];              // 0x00C: Game Code (ASCII, e.g. "ASME", "NTR-")
    char maker_code[2];             // 0x010: Maker Code (ASCII, e.g. "01" Nintendo)
    uint8_t unit_code;              // 0x012: 0x00=NDS, 0x02=NDS+DSi, 0x03=DSi
    uint8_t device_type;            // 0x013: Device type (D15-D8 of ROM chip ID)
    uint8_t device_capacity;        // 0x014: Device size = 128KB << device_capacity (or 1 << (20+N))
    uint8_t reserved1[7];           // 0x015: Reserved
    uint8_t nds_region;             // 0x01C: Region (0x00=Normal, 0x40=Korea, 0x80=China)
    uint8_t rom_version;            // 0x01D: ROM Version
    uint8_t internal_flags;         // 0x01E: Autostart / internal flags
    uint8_t reserved2;              // 0x01F: Reserved
    uint32_t arm9_rom_offset;       // 0x020: ARM9 ROM file offset
    uint32_t arm9_entry_address;    // 0x024: ARM9 execution entry address (usually 0x02000000..0x023FFFFF)
    uint32_t arm9_ram_address;      // 0x028: ARM9 RAM load destination address
    uint32_t arm9_size;             // 0x02C: ARM9 binary size in bytes
    uint32_t arm7_rom_offset;       // 0x030: ARM7 ROM file offset
    uint32_t arm7_entry_address;    // 0x034: ARM7 execution entry address
    uint32_t arm7_ram_address;      // 0x038: ARM7 RAM load destination address
    uint32_t arm7_size;             // 0x03C: ARM7 binary size in bytes
    uint32_t fnt_offset;            // 0x040: Filename Table (FNT) offset
    uint32_t fnt_size;              // 0x044: Filename Table size
    uint32_t fat_offset;            // 0x048: File Allocation Table (FAT) offset
    uint32_t fat_size;              // 0x04C: File Allocation Table size
    uint32_t arm9_overlay_offset;   // 0x050: ARM9 Overlay table offset
    uint32_t arm9_overlay_size;     // 0x054: ARM9 Overlay table size
    uint32_t arm7_overlay_offset;   // 0x058: ARM7 Overlay table offset
    uint32_t arm7_overlay_size;     // 0x05C: ARM7 Overlay table size
    uint32_t rom_control_normal;    // 0x060: Port 40001A4h normal mode settings
    uint32_t rom_control_key1;      // 0x064: Port 40001A4h KEY1 mode settings
    uint32_t banner_offset;         // 0x068: Icon and Title Banner offset
    uint16_t secure_area_crc;       // 0x06C: Secure Area CRC16
    uint16_t secure_area_delay;     // 0x06E: Secure Area transfer delay
    uint32_t arm9_autoload_hook;    // 0x070: ARM9 Autoload list RAM address
    uint32_t arm7_autoload_hook;    // 0x074: ARM7 Autoload list RAM address
    uint64_t secure_area_disable;   // 0x078: Secure Area disable magic (8 bytes)
    uint32_t total_rom_size;        // 0x080: Total ROM size in bytes (excluding DSi area)
    uint32_t header_size;           // 0x084: Header size (usually 0x4000)
    uint8_t reserved3[0x38];        // 0x088..0x0BF: Reserved
    uint8_t nintendo_logo[0x9C];    // 0x0C0..0x15B: Nintendo Logo bitmap
    uint16_t logo_crc;              // 0x15C: Nintendo Logo CRC16
    uint16_t header_crc;            // 0x15E: Header CRC16 (0x000..0x15D)
    uint8_t debug_reserved[0x20];   // 0x160..0x17F: Debug reserved
    uint8_t reserved4[0x80];        // 0x180..0x1FF: Reserved
};
#pragma pack(pop)

static_assert(sizeof(RawNDSHeader) == 0x200, "RawNDSHeader size must be exactly 512 bytes (0x200)");

struct ParsedHeader {
    std::string game_title;
    std::string game_code;
    std::string maker_code;
    uint8_t unit_code{0};
    uint8_t device_capacity{0};
    uint64_t chip_capacity_bytes{0};
    uint8_t rom_version{0};
    uint8_t region{0};

    uint32_t arm9_rom_offset{0};
    uint32_t arm9_entry_address{0};
    uint32_t arm9_ram_address{0};
    uint32_t arm9_size{0};

    uint32_t arm7_rom_offset{0};
    uint32_t arm7_entry_address{0};
    uint32_t arm7_ram_address{0};
    uint32_t arm7_size{0};

    uint32_t fnt_offset{0};
    uint32_t fnt_size{0};
    uint32_t fat_offset{0};
    uint32_t fat_size{0};

    uint32_t arm9_overlay_offset{0};
    uint32_t arm9_overlay_size{0};
    uint32_t arm7_overlay_offset{0};
    uint32_t arm7_overlay_size{0};

    uint32_t banner_offset{0};
    uint16_t secure_area_crc{0};
    uint16_t header_crc{0};
    uint16_t calculated_header_crc{0};
    bool header_crc_valid{false};
    uint32_t total_rom_size{0};

    [[nodiscard]] std::string to_json(int indent = 2) const;
    [[nodiscard]] std::string summary() const;
};

class NDSHeaderParser {
public:
    static std::optional<ParsedHeader> parse(std::span<const uint8_t> data, std::string* error_msg = nullptr);
    static uint16_t calculate_crc16(std::span<const uint8_t> data, uint16_t init_val = 0xFFFF);
};

} // namespace descomp::nds
