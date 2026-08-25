#pragma once

#include "nds/nds_header.h"
#include "nds/nds_overlay.h"
#include "nds/nitrofs.h"
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
#include <span>

namespace descomp::nds {

class NDSImage {
public:
    NDSImage() = default;
    ~NDSImage() = default;

    static std::unique_ptr<NDSImage> load_from_file(const std::filesystem::path& path, std::string* error_msg = nullptr);
    static std::unique_ptr<NDSImage> load_from_memory(std::vector<uint8_t> buffer, std::string* error_msg = nullptr);

    [[nodiscard]] const ParsedHeader& header() const { return m_header; }
    [[nodiscard]] const std::vector<uint8_t>& raw_data() const { return m_raw_data; }

    [[nodiscard]] std::span<const uint8_t> arm9_binary() const;
    [[nodiscard]] std::span<const uint8_t> arm7_binary() const;

    [[nodiscard]] const std::vector<OverlayInfo>& arm9_overlays() const { return m_arm9_overlays; }
    [[nodiscard]] const std::vector<OverlayInfo>& arm7_overlays() const { return m_arm7_overlays; }
    [[nodiscard]] const NitroFSInfo& nitrofs() const { return m_nitrofs; }

    [[nodiscard]] bool extract_to(const std::filesystem::path& output_dir, std::string* log_msg = nullptr) const;

private:
    bool init(std::string* error_msg);

    std::vector<uint8_t> m_raw_data;
    ParsedHeader m_header;
    std::vector<OverlayInfo> m_arm9_overlays;
    std::vector<OverlayInfo> m_arm7_overlays;
    NitroFSInfo m_nitrofs;
};

using NDSROM = NDSImage;

} // namespace descomp::nds
