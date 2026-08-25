#include "test_framework.h"
#include "nds/nds_header.h"
#include "nds/nds_image.h"
#include "nds/nds_overlay.h"
#include "nds/nitrofs.h"
#include <cstring>
#include <filesystem>

using namespace descomp::nds;

static std::vector<uint8_t> create_synthetic_nds_rom() {
    // Construct a synthetic 64KB NDS ROM buffer
    std::vector<uint8_t> rom(65536, 0x00);

    RawNDSHeader header;
    std::memset(&header, 0, sizeof(header));

    std::memcpy(header.game_title, "TESTGAME\0\0\0\0", 12);
    std::memcpy(header.game_code, "NTR-", 4);
    std::memcpy(header.maker_code, "01", 2);
    header.unit_code = 0x00;
    header.device_capacity = 0x07; // 16 MB

    // ARM9: at offset 0x1000, size 0x800 bytes, load at 0x02000000
    header.arm9_rom_offset = 0x1000;
    header.arm9_entry_address = 0x02000000;
    header.arm9_ram_address = 0x02000000;
    header.arm9_size = 0x0800;

    // Put some valid ARM instructions into ARM9
    // 0x02000000: MOV R0, #0 (0xE3A00000)
    // 0x02000004: BX LR      (0xE12FFF1E)
    uint32_t inst1 = 0xE3A00000;
    uint32_t inst2 = 0xE12FFF1E;
    std::memcpy(rom.data() + 0x1000, &inst1, 4);
    std::memcpy(rom.data() + 0x1004, &inst2, 4);

    // ARM7: at offset 0x2000, size 0x400 bytes, load at 0x03800000
    header.arm7_rom_offset = 0x2000;
    header.arm7_entry_address = 0x03800000;
    header.arm7_ram_address = 0x03800000;
    header.arm7_size = 0x0400;

    // FAT Table: at offset 0x3000, size 16 bytes (2 files: file 0 is overlay_000 at 0x4000..0x4100, file 1 is data at 0x4100..0x4200)
    header.fat_offset = 0x3000;
    header.fat_size = 16;
    uint32_t fat_entries[4] = {
        0x00004000, 0x00004100, // File 0
        0x00004100, 0x00004200  // File 1
    };
    std::memcpy(rom.data() + 0x3000, fat_entries, sizeof(fat_entries));

    // ARM9 Overlay Table: at offset 0x3100, size 32 bytes (1 overlay entry)
    header.arm9_overlay_offset = 0x3100;
    header.arm9_overlay_size = 32;
    RawOverlayEntry ov_entry;
    std::memset(&ov_entry, 0, sizeof(ov_entry));
    ov_entry.overlay_id = 0;
    ov_entry.ram_address = 0x02100000;
    ov_entry.ram_size = 0x100;
    ov_entry.bss_size = 0x20;
    ov_entry.static_init_start = 0x02100000;
    ov_entry.static_init_end = 0x021000FC;
    ov_entry.file_id = 0;
    ov_entry.flags = 0;
    std::memcpy(rom.data() + 0x3100, &ov_entry, sizeof(ov_entry));

    // Calculate CRC16 for header
    std::memcpy(rom.data(), &header, sizeof(header));
    header.header_crc = NDSHeaderParser::calculate_crc16(std::span<const uint8_t>(rom.data(), 0x15E));
    std::memcpy(rom.data(), &header, sizeof(header));

    return rom;
}

TEST_CASE(NDSROMParser, HeaderValidationAndCRC) {
    auto rom_data = create_synthetic_nds_rom();
    std::string err;
    auto image = NDSImage::load_from_memory(rom_data, &err);

    ASSERT_TRUE(image != nullptr);
    const auto& hdr = image->header();

    ASSERT_EQ(hdr.game_title, "TESTGAME");
    ASSERT_EQ(hdr.game_code, "NTR-");
    ASSERT_EQ(hdr.maker_code, "01");
    ASSERT_TRUE(hdr.header_crc_valid);
    ASSERT_EQ(hdr.arm9_ram_address, 0x02000000U);
    ASSERT_EQ(hdr.arm9_size, 0x0800U);
    ASSERT_EQ(hdr.arm7_ram_address, 0x03800000U);
    ASSERT_EQ(hdr.arm7_size, 0x0400U);
}

TEST_CASE(NDSROMParser, BinarySlicesAndOverlays) {
    auto rom_data = create_synthetic_nds_rom();
    std::string err;
    auto image = NDSImage::load_from_memory(rom_data, &err);
    ASSERT_TRUE(image != nullptr);

    auto arm9_bin = image->arm9_binary();
    ASSERT_EQ(arm9_bin.size(), 0x0800);
    uint32_t first_inst = 0;
    std::memcpy(&first_inst, arm9_bin.data(), 4);
    ASSERT_EQ(first_inst, 0xE3A00000U); // MOV R0, #0

    auto arm7_bin = image->arm7_binary();
    ASSERT_EQ(arm7_bin.size(), 0x0400);

    const auto& overlays = image->arm9_overlays();
    ASSERT_EQ(overlays.size(), 1);
    ASSERT_EQ(overlays[0].id, 0U);
    ASSERT_EQ(overlays[0].ram_address, 0x02100000U);
    ASSERT_EQ(overlays[0].rom_size, 0x100U);
    ASSERT_EQ(overlays[0].data.size(), 0x100);
}

TEST_CASE(NDSROMParser, ErrorHandlingOnTruncatedData) {
    std::vector<uint8_t> tiny_data(256, 0x00);
    std::string err;
    auto image = NDSImage::load_from_memory(tiny_data, &err);
    ASSERT_TRUE(image == nullptr);
    ASSERT_STR_CONTAINS(err, "smaller than required");
}

TEST_CASE(NDSROMParser, ExtractToDirectory) {
    auto rom_data = create_synthetic_nds_rom();
    std::string err;
    auto image = NDSImage::load_from_memory(rom_data, &err);
    ASSERT_TRUE(image != nullptr);

    std::filesystem::path test_out = "./test_output";
    std::string log_msg;
    bool ok = image->extract_to(test_out, &log_msg);
    ASSERT_TRUE(ok);

    ASSERT_TRUE(std::filesystem::exists(test_out / "rom_info.json"));
    ASSERT_TRUE(std::filesystem::exists(test_out / "arm9.bin"));
    ASSERT_TRUE(std::filesystem::exists(test_out / "arm7.bin"));
    ASSERT_TRUE(std::filesystem::exists(test_out / "overlays" / "overlay_000.bin"));

    // Cleanup
    std::error_code ec;
    std::filesystem::remove_all(test_out, ec);
}
