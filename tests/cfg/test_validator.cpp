#include "test_framework.h"
#include "analysis/validator.h"
#include "nds/nds_image.h"
#include "arm/arm_decoder.h"
#include <vector>
#include <cstring>

using namespace descomp::analysis;
using namespace descomp::arm;
using namespace descomp::nds;

TEST_CASE(Validator, ValidatesCleanCodeWithoutErrors) {
    // Construct a synthetic NDS ROM in memory
    std::vector<uint8_t> rom(65536, 0x00);
    RawNDSHeader header;
    std::memset(&header, 0, sizeof(header));
    std::memcpy(header.game_title, "CLEAN_TEST\0\0", 12);
    header.arm9_rom_offset = 0x1000;
    header.arm9_entry_address = 0x02000000;
    header.arm9_ram_address = 0x02000000;
    header.arm9_size = 0x100;

    // ARM9 clean code:
    // 0x02000000: MOV R0, #0 (0xE3A00000)
    // 0x02000004: BX LR      (0xE12FFF1E)
    uint32_t c1 = 0xE3A00000;
    uint32_t c2 = 0xE12FFF1E;
    std::memcpy(rom.data() + 0x1000, &c1, 4);
    std::memcpy(rom.data() + 0x1004, &c2, 4);

    std::string err;
    auto image = NDSImage::load_from_memory(rom, &err);
    ASSERT_TRUE(image != nullptr);

    auto report = Phase1Validator::validate_rom(*image);
    ASSERT_EQ(report.global_stats.invalid_blocks, 0U);
    ASSERT_EQ(report.global_stats.duplicate_functions, 0U);
    ASSERT_EQ(report.global_stats.functions_without_entry, 0U);
    ASSERT_TRUE(report.global_stats.functions >= 1);
}

TEST_CASE(Validator, DetectsInvalidBranchTargets) {
    std::vector<uint8_t> rom(65536, 0x00);
    RawNDSHeader header;
    std::memset(&header, 0, sizeof(header));
    std::memcpy(header.game_title, "BAD_BRANCH\0\0", 12);
    header.arm9_rom_offset = 0x1000;
    header.arm9_entry_address = 0x02000000;
    header.arm9_ram_address = 0x02000000;
    header.arm9_size = 0x100;

    // Branch to invalid address 0x55555554 (outside all NDS RAM):
    // Target = 0x02000000 + 8 + offset -> offset = 0x5355554C
    // B 0x55555554: 0xEA000000 | ((0x5355554C >> 2) & 0x00FFFFFF)
    uint32_t raw_bad_branch = 0xEA000000 | ((0x5355554C >> 2) & 0x00FFFFFF);
    std::memcpy(rom.data() + 0x1000, &raw_bad_branch, 4);

    std::string err;
    auto image = NDSImage::load_from_memory(rom, &err);
    ASSERT_TRUE(image != nullptr);

    auto report = Phase1Validator::validate_rom(*image);
    ASSERT_TRUE(report.global_stats.invalid_targets > 0);
}
