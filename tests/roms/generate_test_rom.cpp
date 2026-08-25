#include "nds/nds_header.h"
#include "nds/nds_overlay.h"
#include <vector>
#include <fstream>
#include <cstring>
#include <iostream>

using namespace descomp::nds;

int main() {
    std::vector<uint8_t> rom(65536, 0x00);

    RawNDSHeader header;
    std::memset(&header, 0, sizeof(header));

    std::memcpy(header.game_title, "DEMO GAME\0\0\0", 12);
    std::memcpy(header.game_code, "NTR-ADEM", 4);
    std::memcpy(header.maker_code, "01", 2);
    header.unit_code = 0x00;
    header.device_capacity = 0x07; // 16 MB

    // ARM9 section at 0x1000, 0x800 bytes, RAM 0x02000000
    header.arm9_rom_offset = 0x1000;
    header.arm9_entry_address = 0x02000000;
    header.arm9_ram_address = 0x02000000;
    header.arm9_size = 0x0800;

    // ARM9 Instructions:
    // func_02000000:
    // 0x02000000: E3A00000  MOV R0, #0
    // 0x02000004: E3A01001  MOV R1, #1
    // 0x02000008: E0802001  ADD R2, R0, R1
    // 0x0200000C: E3520005  CMP R2, #5
    // 0x02000010: 1A000002  BNE 0x02000020 (offset (0x20 - 0x18)/4 = 2)
    // 0x02000014: EB00000D  BL  0x02000050 (offset (0x50 - 0x1C)/4 = 0x34/4 = 13 = 0xD)
    // 0x02000018: E3A0000A  MOV R0, #10
    // 0x0200001C: E12FFF1E  BX  LR
    // 0x02000020: E2422001  SUB R2, R2, #1
    // 0x02000024: E12FFF1E  BX  LR
    // ...
    // func_02000050:
    // 0x02000050: E92D4010  PUSH {R4, LR}
    // 0x02000054: E3A0402A  MOV  R4, #42
    // 0x02000058: E8BD8010  POP  {R4, PC}

    uint32_t a9_code[] = {
        0xE3A00000, 0xE3A01001, 0xE0802001, 0xE3520005,
        0x1A000002, 0xEB00000D, 0xE3A0000A, 0xE12FFF1E,
        0xE2422001, 0xE12FFF1E,
        0x00000000, 0x00000000, 0x00000000, 0x00000000, // padding to 0x50
        0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0x00000000, 0x00000000,
        0xE92D4010, 0xE3A0402A, 0xE8BD8010
    };
    std::memcpy(rom.data() + 0x1000, a9_code, sizeof(a9_code));

    // ARM7 section at 0x2000, 0x400 bytes, RAM 0x03800000
    header.arm7_rom_offset = 0x2000;
    header.arm7_entry_address = 0x03800000;
    header.arm7_ram_address = 0x03800000;
    header.arm7_size = 0x0400;

    // FAT Table at 0x3000
    header.fat_offset = 0x3000;
    header.fat_size = 16;
    uint32_t fat_entries[4] = {
        0x00004000, 0x00004100, // File 0: Overlay 0
        0x00004100, 0x00004200  // File 1: Data
    };
    std::memcpy(rom.data() + 0x3000, fat_entries, sizeof(fat_entries));

    // ARM9 Overlay Table at 0x3100
    header.arm9_overlay_offset = 0x3100;
    header.arm9_overlay_size = 32;
    RawOverlayEntry ov0;
    std::memset(&ov0, 0, sizeof(ov0));
    ov0.overlay_id = 0;
    ov0.ram_address = 0x02100000;
    ov0.ram_size = 0x100;
    ov0.bss_size = 0x20;
    ov0.static_init_start = 0x02100000;
    ov0.static_init_end = 0x021000FC;
    ov0.file_id = 0;
    std::memcpy(rom.data() + 0x3100, &ov0, sizeof(ov0));

    // Overlay 0 code at 0x4000
    uint32_t ov_code[] = {
        0xE3A000FF, // MOV R0, #255
        0xE12FFF1E  // BX LR
    };
    std::memcpy(rom.data() + 0x4000, ov_code, sizeof(ov_code));

    // Calculate CRC16
    std::memcpy(rom.data(), &header, sizeof(header));
    header.header_crc = NDSHeaderParser::calculate_crc16(std::span<const uint8_t>(rom.data(), 0x15E));
    std::memcpy(rom.data(), &header, sizeof(header));

    std::ofstream out("tests/roms/demo_game.nds", std::ios::binary);
    if (!out.is_open()) {
        std::cerr << "Failed to open demo_game.nds for writing\n";
        return 1;
    }
    out.write(reinterpret_cast<const char*>(rom.data()), rom.size());
    std::cout << "Successfully generated tests/roms/demo_game.nds (64 KB)\n";
    return 0;
}
