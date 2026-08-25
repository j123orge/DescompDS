#include "nds/nds_overlay.h"
#include <cstring>
#include <sstream>
#include <iomanip>

namespace descomp::nds {

static std::string hex32(uint32_t val) {
    std::ostringstream ss;
    ss << "0x" << std::hex << std::uppercase << std::setw(8) << std::setfill('0') << val;
    return ss.str();
}

std::string OverlayInfo::to_json(int indent) const {
    std::string ind(indent, ' ');
    std::ostringstream ss;
    ss << "{\n";
    ss << ind << "\"id\": " << id << ",\n";
    ss << ind << "\"ram_address\": \"" << hex32(ram_address) << "\",\n";
    ss << ind << "\"ram_size\": " << ram_size << ",\n";
    ss << ind << "\"bss_size\": " << bss_size << ",\n";
    ss << ind << "\"static_init_start\": \"" << hex32(static_init_start) << "\",\n";
    ss << ind << "\"static_init_end\": \"" << hex32(static_init_end) << "\",\n";
    ss << ind << "\"file_id\": " << file_id << ",\n";
    ss << ind << "\"compressed\": " << (compressed ? "true" : "false") << ",\n";
    ss << ind << "\"rom_offset\": " << rom_offset << ",\n";
    ss << ind << "\"rom_size\": " << rom_size << "\n";
    ss << "}";
    return ss.str();
}

std::vector<OverlayInfo> NDSOverlayParser::parse_table(
    std::span<const uint8_t> overlay_table_data,
    std::span<const uint8_t> fat_table_data,
    std::span<const uint8_t> rom_data,
    std::string* error_msg
) {
    std::vector<OverlayInfo> overlays;
    if (overlay_table_data.empty()) {
        return overlays;
    }

    size_t count = overlay_table_data.size() / sizeof(RawOverlayEntry);
    overlays.reserve(count);

    for (size_t i = 0; i < count; ++i) {
        RawOverlayEntry raw;
        std::memcpy(&raw, overlay_table_data.data() + (i * sizeof(RawOverlayEntry)), sizeof(RawOverlayEntry));

        OverlayInfo info;
        info.id = raw.overlay_id;
        info.ram_address = raw.ram_address;
        info.ram_size = raw.ram_size;
        info.bss_size = raw.bss_size;
        info.static_init_start = raw.static_init_start;
        info.static_init_end = raw.static_init_end;
        info.file_id = raw.file_id;
        info.compressed = ((raw.flags & 0x01000000) != 0);

        // FAT table lookup: each FAT entry is 8 bytes (start_offset, end_offset)
        size_t fat_entry_offset = static_cast<size_t>(raw.file_id) * 8;
        if (fat_entry_offset + 8 <= fat_table_data.size()) {
            uint32_t start_off = 0;
            uint32_t end_off = 0;
            std::memcpy(&start_off, fat_table_data.data() + fat_entry_offset, 4);
            std::memcpy(&end_off, fat_table_data.data() + fat_entry_offset + 4, 4);

            info.rom_offset = start_off;
            if (end_off >= start_off) {
                info.rom_size = end_off - start_off;
            }

            if (start_off <= rom_data.size() && end_off <= rom_data.size() && end_off >= start_off) {
                info.data.assign(rom_data.data() + start_off, rom_data.data() + end_off);
            } else if (error_msg) {
                *error_msg += "Warning: Overlay " + std::to_string(info.id) + " FAT range (" +
                              std::to_string(start_off) + ".." + std::to_string(end_off) +
                              ") is outside ROM bounds.\n";
            }
        } else if (error_msg) {
            *error_msg += "Warning: Overlay " + std::to_string(info.id) + " has invalid FAT file_id " +
                          std::to_string(raw.file_id) + "\n";
        }

        overlays.push_back(std::move(info));
    }

    return overlays;
}

} // namespace descomp::nds
