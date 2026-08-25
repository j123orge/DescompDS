#include "nds/nds_header.h"
#include <cstring>
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace descomp::nds {

uint16_t NDSHeaderParser::calculate_crc16(std::span<const uint8_t> data, uint16_t init_val) {
    // Standard Nintendo CRC-16 (CRC-16-CCITT: poly 0x1021, init 0xFFFF)
    // Used for header (0x000 to 0x15D) and logo (0x0C0 to 0x15B)
    uint32_t crc = init_val;
    for (uint8_t byte : data) {
        crc ^= static_cast<uint32_t>(byte) << 8;
        for (int bit = 0; bit < 8; ++bit) {
            if (crc & 0x8000) {
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF;
            } else {
                crc = (crc << 1) & 0xFFFF;
            }
        }
    }
    return static_cast<uint16_t>(crc);
}

static std::string sanitize_string(const char* data, size_t max_len) {
    std::string result;
    result.reserve(max_len);
    for (size_t i = 0; i < max_len; ++i) {
        char c = data[i];
        if (c == '\0') break;
        if (c >= 32 && c <= 126) {
            result.push_back(c);
        }
    }
    // Trim trailing spaces
    while (!result.empty() && result.back() == ' ') {
        result.pop_back();
    }
    return result;
}

std::optional<ParsedHeader> NDSHeaderParser::parse(std::span<const uint8_t> data, std::string* error_msg) {
    if (data.size() < sizeof(RawNDSHeader)) {
        if (error_msg) {
            *error_msg = "Data buffer smaller than required NDS header size (512 bytes). Got: " +
                         std::to_string(data.size());
        }
        return std::nullopt;
    }

    RawNDSHeader raw;
    std::memcpy(&raw, data.data(), sizeof(RawNDSHeader));

    ParsedHeader parsed;
    parsed.game_title = sanitize_string(raw.game_title, sizeof(raw.game_title));
    parsed.game_code = sanitize_string(raw.game_code, sizeof(raw.game_code));
    parsed.maker_code = sanitize_string(raw.maker_code, sizeof(raw.maker_code));
    parsed.unit_code = raw.unit_code;
    parsed.device_capacity = raw.device_capacity;
    
    // Device capacity calculation: 128KB << device_capacity (e.g. 0x07 = 16MB, 0x08 = 32MB, 0x09 = 64MB)
    if (raw.device_capacity <= 30) {
        parsed.chip_capacity_bytes = 128ULL * 1024ULL * (1ULL << raw.device_capacity);
    } else {
        parsed.chip_capacity_bytes = 0;
    }

    parsed.rom_version = raw.rom_version;
    parsed.region = raw.nds_region;

    parsed.arm9_rom_offset = raw.arm9_rom_offset;
    parsed.arm9_entry_address = raw.arm9_entry_address;
    parsed.arm9_ram_address = raw.arm9_ram_address;
    parsed.arm9_size = raw.arm9_size;

    parsed.arm7_rom_offset = raw.arm7_rom_offset;
    parsed.arm7_entry_address = raw.arm7_entry_address;
    parsed.arm7_ram_address = raw.arm7_ram_address;
    parsed.arm7_size = raw.arm7_size;

    parsed.fnt_offset = raw.fnt_offset;
    parsed.fnt_size = raw.fnt_size;
    parsed.fat_offset = raw.fat_offset;
    parsed.fat_size = raw.fat_size;

    parsed.arm9_overlay_offset = raw.arm9_overlay_offset;
    parsed.arm9_overlay_size = raw.arm9_overlay_size;
    parsed.arm7_overlay_offset = raw.arm7_overlay_offset;
    parsed.arm7_overlay_size = raw.arm7_overlay_size;

    parsed.banner_offset = raw.banner_offset;
    parsed.secure_area_crc = raw.secure_area_crc;
    parsed.header_crc = raw.header_crc;
    parsed.total_rom_size = raw.total_rom_size;

    // Validate Header CRC16 (0x000..0x15D inclusive = 350 bytes)
    std::span<const uint8_t> header_crc_span = data.subspan(0, 0x15E);
    parsed.calculated_header_crc = calculate_crc16(header_crc_span);
    parsed.header_crc_valid = (parsed.calculated_header_crc == parsed.header_crc);

    return parsed;
}

static std::string hex32(uint32_t val) {
    std::ostringstream ss;
    ss << "0x" << std::hex << std::uppercase << std::setw(8) << std::setfill('0') << val;
    return ss.str();
}

static std::string hex16(uint16_t val) {
    std::ostringstream ss;
    ss << "0x" << std::hex << std::uppercase << std::setw(4) << std::setfill('0') << val;
    return ss.str();
}

std::string ParsedHeader::to_json(int indent) const {
    std::string ind(indent, ' ');
    std::ostringstream ss;
    ss << "{\n";
    ss << ind << "\"game_title\": \"" << game_title << "\",\n";
    ss << ind << "\"game_code\": \"" << game_code << "\",\n";
    ss << ind << "\"maker_code\": \"" << maker_code << "\",\n";
    ss << ind << "\"unit_code\": " << static_cast<int>(unit_code) << ",\n";
    ss << ind << "\"device_capacity_id\": " << static_cast<int>(device_capacity) << ",\n";
    ss << ind << "\"chip_capacity_bytes\": " << chip_capacity_bytes << ",\n";
    ss << ind << "\"rom_version\": " << static_cast<int>(rom_version) << ",\n";
    ss << ind << "\"region\": " << static_cast<int>(region) << ",\n";
    ss << ind << "\"arm9\": {\n";
    ss << ind << "  \"rom_offset\": " << arm9_rom_offset << ",\n";
    ss << ind << "  \"rom_offset_hex\": \"" << hex32(arm9_rom_offset) << "\",\n";
    ss << ind << "  \"entry_address\": \"" << hex32(arm9_entry_address) << "\",\n";
    ss << ind << "  \"ram_address\": \"" << hex32(arm9_ram_address) << "\",\n";
    ss << ind << "  \"size\": " << arm9_size << "\n";
    ss << ind << "},\n";
    ss << ind << "\"arm7\": {\n";
    ss << ind << "  \"rom_offset\": " << arm7_rom_offset << ",\n";
    ss << ind << "  \"rom_offset_hex\": \"" << hex32(arm7_rom_offset) << "\",\n";
    ss << ind << "  \"entry_address\": \"" << hex32(arm7_entry_address) << "\",\n";
    ss << ind << "  \"ram_address\": \"" << hex32(arm7_ram_address) << "\",\n";
    ss << ind << "  \"size\": " << arm7_size << "\n";
    ss << ind << "},\n";
    ss << ind << "\"fnt\": {\n";
    ss << ind << "  \"offset\": " << fnt_offset << ",\n";
    ss << ind << "  \"size\": " << fnt_size << "\n";
    ss << ind << "},\n";
    ss << ind << "\"fat\": {\n";
    ss << ind << "  \"offset\": " << fat_offset << ",\n";
    ss << ind << "  \"size\": " << fat_size << "\n";
    ss << ind << "},\n";
    ss << ind << "\"arm9_overlay\": {\n";
    ss << ind << "  \"offset\": " << arm9_overlay_offset << ",\n";
    ss << ind << "  \"size\": " << arm9_overlay_size << ",\n";
    ss << ind << "  \"count\": " << (arm9_overlay_size / 32) << "\n";
    ss << ind << "},\n";
    ss << ind << "\"arm7_overlay\": {\n";
    ss << ind << "  \"offset\": " << arm7_overlay_offset << ",\n";
    ss << ind << "  \"size\": " << arm7_overlay_size << ",\n";
    ss << ind << "  \"count\": " << (arm7_overlay_size / 32) << "\n";
    ss << ind << "},\n";
    ss << ind << "\"banner_offset\": " << banner_offset << ",\n";
    ss << ind << "\"header_crc\": \"" << hex16(header_crc) << "\",\n";
    ss << ind << "\"calculated_header_crc\": \"" << hex16(calculated_header_crc) << "\",\n";
    ss << ind << "\"header_crc_valid\": " << (header_crc_valid ? "true" : "false") << ",\n";
    ss << ind << "\"secure_area_crc\": \"" << hex16(secure_area_crc) << "\",\n";
    ss << ind << "\"total_rom_size\": " << total_rom_size << "\n";
    ss << "}\n";
    return ss.str();
}

std::string ParsedHeader::summary() const {
    std::ostringstream ss;
    ss << "=== NDS Header Summary ===\n";
    ss << "Game Title:          " << game_title << "\n";
    ss << "Game Code:           " << game_code << "\n";
    ss << "Maker Code:          " << maker_code << "\n";
    ss << "ARM9 Entry Address:  " << hex32(arm9_entry_address) << "\n";
    ss << "ARM9 RAM Address:    " << hex32(arm9_ram_address) << "\n";
    ss << "ARM9 ROM Offset:     " << hex32(arm9_rom_offset) << " (Size: " << arm9_size << " bytes)\n";
    ss << "ARM7 Entry Address:  " << hex32(arm7_entry_address) << "\n";
    ss << "ARM7 RAM Address:    " << hex32(arm7_ram_address) << "\n";
    ss << "ARM7 ROM Offset:     " << hex32(arm7_rom_offset) << " (Size: " << arm7_size << " bytes)\n";
    ss << "ARM9 Overlays:       Offset " << hex32(arm9_overlay_offset) << ", Size " << arm9_overlay_size 
       << " (" << (arm9_overlay_size / 32) << " entries)\n";
    ss << "ARM7 Overlays:       Offset " << hex32(arm7_overlay_offset) << ", Size " << arm7_overlay_size 
       << " (" << (arm7_overlay_size / 32) << " entries)\n";
    ss << "FAT Table:           Offset " << hex32(fat_offset) << ", Size " << fat_size << " bytes\n";
    ss << "FNT Table:           Offset " << hex32(fnt_offset) << ", Size " << fnt_size << " bytes\n";
    ss << "Banner Offset:       " << hex32(banner_offset) << "\n";
    ss << "Header CRC:          " << hex16(header_crc) << " (" << (header_crc_valid ? "VALID" : "INVALID / MISMATCH") << ")\n";
    return ss.str();
}

} // namespace descomp::nds
