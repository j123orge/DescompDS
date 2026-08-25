#include "nds/nitrofs.h"
#include <cstring>
#include <sstream>
#include <map>
#include <queue>

namespace descomp::nds {

std::string NitroFSInfo::to_json(int indent) const {
    std::string ind(indent, ' ');
    std::ostringstream ss;
    ss << "{\n";
    ss << ind << "\"total_directories\": " << total_directories << ",\n";
    ss << ind << "\"total_files\": " << total_files << ",\n";
    ss << ind << "\"files\": [\n";
    for (size_t i = 0; i < files.size(); ++i) {
        const auto& f = files[i];
        ss << ind << "  {\n";
        ss << ind << "    \"file_id\": " << f.file_id << ",\n";
        ss << ind << "    \"path\": \"" << f.path << "\",\n";
        ss << ind << "    \"rom_offset\": " << f.rom_offset << ",\n";
        ss << ind << "    \"size\": " << f.size << "\n";
        ss << ind << "  }" << (i + 1 < files.size() ? "," : "") << "\n";
    }
    ss << ind << "]\n";
    ss << "}";
    return ss.str();
}

struct RawFNTDirEntry {
    uint32_t sub_table_offset;
    uint16_t first_file_id;
    uint16_t parent_dir_id;
};

NitroFSInfo NitroFSParser::parse(
    std::span<const uint8_t> fnt_data,
    std::span<const uint8_t> fat_data,
    std::string* error_msg
) {
    NitroFSInfo info;
    if (fnt_data.size() < sizeof(RawFNTDirEntry) || fat_data.empty()) {
        return info;
    }

    RawFNTDirEntry root_entry;
    std::memcpy(&root_entry, fnt_data.data(), sizeof(RawFNTDirEntry));

    // In NDS FNT, parent_dir_id of root entry contains the total number of directories (or 0xF000 count)
    uint16_t total_dirs = root_entry.parent_dir_id & 0x0FFF;
    if (total_dirs == 0) {
        total_dirs = 1;
    }
    info.total_directories = total_dirs;

    size_t dir_table_size = static_cast<size_t>(total_dirs) * sizeof(RawFNTDirEntry);
    if (fnt_data.size() < dir_table_size) {
        if (error_msg) {
            *error_msg = "FNT data smaller than directory table size";
        }
        return info;
    }

    // Map directory IDs to paths
    std::map<uint16_t, std::string> dir_paths;
    dir_paths[0xF000] = "";

    // Queue for BFS traversal of directories: pair(dir_id, dir_index)
    std::queue<std::pair<uint16_t, uint16_t>> work_queue;
    work_queue.push({0xF000, 0});

    while (!work_queue.empty()) {
        auto [cur_dir_id, cur_dir_idx] = work_queue.front();
        work_queue.pop();

        if (cur_dir_idx >= total_dirs) continue;

        RawFNTDirEntry dir_entry;
        std::memcpy(&dir_entry, fnt_data.data() + (cur_dir_idx * sizeof(RawFNTDirEntry)), sizeof(RawFNTDirEntry));

        std::string cur_path = dir_paths[cur_dir_id];
        uint32_t sub_offset = dir_entry.sub_table_offset;
        uint16_t file_id = dir_entry.first_file_id;

        while (sub_offset < fnt_data.size()) {
            uint8_t len_byte = fnt_data[sub_offset++];
            if (len_byte == 0x00) {
                // End of sub-table
                break;
            }

            bool is_dir = (len_byte & 0x80) != 0;
            uint8_t name_len = len_byte & 0x7F;

            if (sub_offset + name_len > fnt_data.size()) {
                break;
            }

            std::string name(reinterpret_cast<const char*>(fnt_data.data() + sub_offset), name_len);
            sub_offset += name_len;

            if (is_dir) {
                if (sub_offset + 2 > fnt_data.size()) break;
                uint16_t sub_dir_id = 0;
                std::memcpy(&sub_dir_id, fnt_data.data() + sub_offset, 2);
                sub_offset += 2;

                uint16_t sub_dir_idx = sub_dir_id & 0x0FFF;
                std::string sub_path = cur_path.empty() ? name : cur_path + "/" + name;
                dir_paths[sub_dir_id] = sub_path;
                work_queue.push({sub_dir_id, sub_dir_idx});
            } else {
                NitroFile file;
                file.file_id = file_id++;
                file.path = cur_path.empty() ? name : cur_path + "/" + name;

                // Lookup in FAT
                size_t fat_offset = static_cast<size_t>(file.file_id) * 8;
                if (fat_offset + 8 <= fat_data.size()) {
                    uint32_t start_off = 0;
                    uint32_t end_off = 0;
                    std::memcpy(&start_off, fat_data.data() + fat_offset, 4);
                    std::memcpy(&end_off, fat_data.data() + fat_offset + 4, 4);
                    file.rom_offset = start_off;
                    if (end_off >= start_off) {
                        file.size = end_off - start_off;
                    }
                }
                info.files.push_back(file);
            }
        }
    }

    info.total_files = static_cast<uint32_t>(info.files.size());
    return info;
}

} // namespace descomp::nds
