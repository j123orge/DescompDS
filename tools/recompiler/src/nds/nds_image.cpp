#include "nds/nds_image.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <iostream>

namespace descomp::nds {

std::unique_ptr<NDSImage> NDSImage::load_from_file(const std::filesystem::path& path, std::string* error_msg) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        if (error_msg) {
            *error_msg = "Failed to open NDS ROM file: " + path.string();
        }
        return nullptr;
    }

    auto file_size = file.tellg();
    if (file_size < 512) {
        if (error_msg) {
            *error_msg = "File is too small to be a valid NDS ROM (less than 512 bytes). Size: " +
                         std::to_string(file_size);
        }
        return nullptr;
    }

    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> buffer(static_cast<size_t>(file_size));
    if (!file.read(reinterpret_cast<char*>(buffer.data()), file_size)) {
        if (error_msg) {
            *error_msg = "Failed to read file content: " + path.string();
        }
        return nullptr;
    }

    return load_from_memory(std::move(buffer), error_msg);
}

std::unique_ptr<NDSImage> NDSImage::load_from_memory(std::vector<uint8_t> buffer, std::string* error_msg) {
    auto image = std::make_unique<NDSImage>();
    image->m_raw_data = std::move(buffer);
    if (!image->init(error_msg)) {
        return nullptr;
    }
    return image;
}

bool NDSImage::init(std::string* error_msg) {
    auto parsed = NDSHeaderParser::parse(m_raw_data, error_msg);
    if (!parsed) {
        return false;
    }
    m_header = *parsed;

    // Bounds checking for ARM9
    if (static_cast<uint64_t>(m_header.arm9_rom_offset) + m_header.arm9_size > m_raw_data.size()) {
        if (error_msg) {
            *error_msg = "ARM9 binary exceeds ROM file bounds: offset=" +
                         std::to_string(m_header.arm9_rom_offset) + ", size=" +
                         std::to_string(m_header.arm9_size) + ", rom_size=" +
                         std::to_string(m_raw_data.size());
        }
        return false;
    }

    // Bounds checking for ARM7
    if (static_cast<uint64_t>(m_header.arm7_rom_offset) + m_header.arm7_size > m_raw_data.size()) {
        if (error_msg) {
            *error_msg = "ARM7 binary exceeds ROM file bounds: offset=" +
                         std::to_string(m_header.arm7_rom_offset) + ", size=" +
                         std::to_string(m_header.arm7_size) + ", rom_size=" +
                         std::to_string(m_raw_data.size());
        }
        return false;
    }

    // Parse FAT
    std::span<const uint8_t> fat_span;
    if (m_header.fat_offset > 0 && m_header.fat_size > 0 &&
        static_cast<uint64_t>(m_header.fat_offset) + m_header.fat_size <= m_raw_data.size()) {
        fat_span = std::span<const uint8_t>(m_raw_data.data() + m_header.fat_offset, m_header.fat_size);
    }

    // Parse FNT / NitroFS
    if (m_header.fnt_offset > 0 && m_header.fnt_size > 0 &&
        static_cast<uint64_t>(m_header.fnt_offset) + m_header.fnt_size <= m_raw_data.size() &&
        !fat_span.empty()) {
        std::span<const uint8_t> fnt_span(m_raw_data.data() + m_header.fnt_offset, m_header.fnt_size);
        m_nitrofs = NitroFSParser::parse(fnt_span, fat_span, error_msg);
    }

    // Parse ARM9 Overlays
    if (m_header.arm9_overlay_offset > 0 && m_header.arm9_overlay_size > 0 &&
        static_cast<uint64_t>(m_header.arm9_overlay_offset) + m_header.arm9_overlay_size <= m_raw_data.size() &&
        !fat_span.empty()) {
        std::span<const uint8_t> arm9_ov_span(m_raw_data.data() + m_header.arm9_overlay_offset, m_header.arm9_overlay_size);
        m_arm9_overlays = NDSOverlayParser::parse_table(arm9_ov_span, fat_span, m_raw_data, error_msg);
    }

    // Parse ARM7 Overlays
    if (m_header.arm7_overlay_offset > 0 && m_header.arm7_overlay_size > 0 &&
        static_cast<uint64_t>(m_header.arm7_overlay_offset) + m_header.arm7_overlay_size <= m_raw_data.size() &&
        !fat_span.empty()) {
        std::span<const uint8_t> arm7_ov_span(m_raw_data.data() + m_header.arm7_overlay_offset, m_header.arm7_overlay_size);
        m_arm7_overlays = NDSOverlayParser::parse_table(arm7_ov_span, fat_span, m_raw_data, error_msg);
    }

    return true;
}

std::span<const uint8_t> NDSImage::arm9_binary() const {
    if (static_cast<uint64_t>(m_header.arm9_rom_offset) + m_header.arm9_size <= m_raw_data.size()) {
        return std::span<const uint8_t>(m_raw_data.data() + m_header.arm9_rom_offset, m_header.arm9_size);
    }
    return {};
}

std::span<const uint8_t> NDSImage::arm7_binary() const {
    if (static_cast<uint64_t>(m_header.arm7_rom_offset) + m_header.arm7_size <= m_raw_data.size()) {
        return std::span<const uint8_t>(m_raw_data.data() + m_header.arm7_rom_offset, m_header.arm7_size);
    }
    return {};
}

static bool write_binary_file(const std::filesystem::path& path, std::span<const uint8_t> data) {
    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) return false;
    out.write(reinterpret_cast<const char*>(data.data()), data.size());
    return out.good();
}

static bool write_text_file(const std::filesystem::path& path, const std::string& text) {
    std::ofstream out(path);
    if (!out.is_open()) return false;
    out << text;
    return out.good();
}

bool NDSImage::extract_to(const std::filesystem::path& output_dir, std::string* log_msg) const {
    std::error_code ec;
    std::filesystem::create_directories(output_dir, ec);
    if (ec) {
        if (log_msg) *log_msg = "Failed to create output directory: " + output_dir.string();
        return false;
    }

    // 1. Write rom_info.json
    std::ostringstream json_ss;
    json_ss << "{\n";
    json_ss << "  \"header\": " << m_header.to_json(4) << ",\n";
    json_ss << "  \"arm9_overlays\": [\n";
    for (size_t i = 0; i < m_arm9_overlays.size(); ++i) {
        json_ss << "    " << m_arm9_overlays[i].to_json(4) << (i + 1 < m_arm9_overlays.size() ? "," : "") << "\n";
    }
    json_ss << "  ],\n";
    json_ss << "  \"arm7_overlays\": [\n";
    for (size_t i = 0; i < m_arm7_overlays.size(); ++i) {
        json_ss << "    " << m_arm7_overlays[i].to_json(4) << (i + 1 < m_arm7_overlays.size() ? "," : "") << "\n";
    }
    json_ss << "  ],\n";
    json_ss << "  \"nitrofs\": " << m_nitrofs.to_json(4) << "\n";
    json_ss << "}\n";

    if (!write_text_file(output_dir / "rom_info.json", json_ss.str())) {
        if (log_msg) *log_msg = "Failed to write rom_info.json";
        return false;
    }

    // 2. Extract arm9.bin
    auto a9 = arm9_binary();
    if (!a9.empty()) {
        write_binary_file(output_dir / "arm9.bin", a9);
    }

    // 3. Extract arm7.bin
    auto a7 = arm7_binary();
    if (!a7.empty()) {
        write_binary_file(output_dir / "arm7.bin", a7);
    }

    // 4. Extract Overlays
    if (!m_arm9_overlays.empty()) {
        std::filesystem::path overlays_dir = output_dir / "overlays";
        std::filesystem::create_directories(overlays_dir, ec);

        for (const auto& ov : m_arm9_overlays) {
            std::ostringstream name_ss;
            name_ss << "overlay_" << std::setw(3) << std::setfill('0') << ov.id << ".bin";
            if (!ov.data.empty()) {
                write_binary_file(overlays_dir / name_ss.str(), ov.data);
            }
        }
    }

    if (log_msg) {
        *log_msg = "Successfully extracted NDS components to " + output_dir.string();
    }
    return true;
}

} // namespace descomp::nds
