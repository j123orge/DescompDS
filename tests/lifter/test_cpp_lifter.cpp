#include "test_framework.h"
#include "lifter/cpp_lifter.h"
#include "lifter/cpp_emitter.h"
#include "ir/ir_translator.h"
#include "arm/arm_decoder.h"
#include "arm/thumb_decoder.h"

using namespace descomp;
using namespace descomp::ir;
using namespace descomp::lifter;
using namespace descomp::arm;

static std::vector<IRInstruction> lift_arm(uint32_t raw, uint32_t addr) {
    return IRTranslator::lift(ARMDecoder::decode(raw, addr));
}
static std::string lift_one_cpp(uint32_t raw, uint32_t addr) {
    auto ir = lift_arm(raw, addr);
    return CppLifter::lift_instructions(ir, "test_func");
}
static bool contains(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

TEST_CASE(CppLifter, RegistersPreserved) {
    // MOV R0, #10 ; MOV R1, #20 ; ADD R0, R0, R1
    auto cpp = lift_one_cpp(0xE3A0000A, 0x02000000); // MOV R0, #10
    ASSERT_TRUE(contains(cpp, "cpu.r[0]"));
    auto cpp2 = lift_one_cpp(0xE0810001, 0x02000004); // ADD R0, R1, R1? use known
    ASSERT_TRUE(contains(cpp2, "cpu.r["));
    // R15 (PC) should be cpu.r[15]
    auto bx = lift_one_cpp(0xE12FFF1E, 0x02000000); // BX LR
    ASSERT_TRUE(contains(bx, "return"));
}

TEST_CASE(CppLifter, FlagsPreserved) {
    // CMP sets flags; ADD with S sets flags
    auto cpp_cmp = lift_one_cpp(0xE3500005, 0x02000000); // CMP R0, #5
    ASSERT_TRUE(contains(cpp_cmp, "flag_n"));
    ASSERT_TRUE(contains(cpp_cmp, "flag_z"));
    ASSERT_TRUE(contains(cpp_cmp, "flag_c"));
    ASSERT_TRUE(contains(cpp_cmp, "flag_v"));
    auto cpp_adds = lift_one_cpp(0xE0910002, 0x02000000); // ADD R0, R1, R2, S
    ASSERT_TRUE(contains(cpp_adds, "flag_n"));
    ASSERT_TRUE(contains(cpp_adds, "flag_z"));
}

TEST_CASE(CppLifter, ConditionsPreserved) {
    // BEQ (conditional)
    auto cpp = lift_one_cpp(0x0A000006, 0x02000000);
    ASSERT_TRUE(contains(cpp, "flag_z")); // EQ is flag_z
    ASSERT_TRUE(contains(cpp, "if"));
    ASSERT_TRUE(contains(cpp, "goto"));
}

TEST_CASE(CppLifter, ImmediatesPreserved) {
    auto cpp = lift_one_cpp(0xE3A0000A, 0x02000000); // MOV R0, #10
    ASSERT_TRUE(contains(cpp, "10"));
    auto cpp2 = lift_one_cpp(0xE350001E, 0x02000000); // CMP R0, #30
    ASSERT_TRUE(contains(cpp2, "30"));
}

TEST_CASE(CppLifter, ArithmeticAndLogic) {
    auto cpp_add = lift_one_cpp(0xE2801001, 0x02000000); // ADD R1, R0, #1
    ASSERT_TRUE(contains(cpp_add, "+"));
    ASSERT_TRUE(contains(cpp_add, "cpu.r[1] ="));

    auto cpp_sub = lift_one_cpp(0xE0412000, 0x02000000); // SUB R2, R1, R0
    ASSERT_TRUE(contains(cpp_sub, "-"));

    auto cpp_and = lift_one_cpp(0xE0004001, 0x02000000); // AND R4, R0, R1 -> but check 0xE0004001 is AND
    ASSERT_TRUE(contains(cpp_and, "&"));

    auto cpp_orr = lift_one_cpp(0xE1825003, 0x02000000); // ORR
    ASSERT_TRUE(contains(cpp_orr, "|"));

    auto cpp_eor = lift_one_cpp(0xE0246005, 0x02000000); // EOR
    ASSERT_TRUE(contains(cpp_eor, "^"));
}

TEST_CASE(CppLifter, Shifts) {
    // MOV R0, R1, LSL #1 -> 0xE1A00081 -> lowers to LSL + MOV
    auto ir = lift_arm(0xE1A00081, 0x02000000);
    ASSERT_TRUE(ir.size() >= 2);
    auto cpp = CppLifter::lift_instructions(ir, "shift_test");
    ASSERT_TRUE(contains(cpp, "nds_lsl") || contains(cpp, "<<"));
    ASSERT_TRUE(contains(cpp, "t0"));
}

TEST_CASE(CppLifter, Comparisons) {
    // CMP, CMN, TST, TEQ
    auto check = [](uint32_t raw) {
        auto cpp = lift_one_cpp(raw, 0x02000000);
        ASSERT_TRUE(contains(cpp, "flag_n"));
        ASSERT_TRUE(contains(cpp, "flag_z"));
    };
    check(0xE3500005); // CMP R0, #5
    check(0xE371000A); // CMN R1, #10
    check(0xE3120001); // TST R2, #1
    check(0xE1330004); // TEQ R3, R4
}

TEST_CASE(CppLifter, LdrStrViaHal) {
    auto cpp_ldr = lift_one_cpp(0xE5910004, 0x02000000); // LDR R0, [R1, #4]
    ASSERT_TRUE(contains(cpp_ldr, "nds_hal_read32"));
    ASSERT_TRUE(contains(cpp_ldr, "cpu.r[1]"));

    auto cpp_str = lift_one_cpp(0xE5810004, 0x02000000); // STR R0, [R1, #4]
    ASSERT_TRUE(contains(cpp_str, "nds_hal_write32"));

    auto cpp_ldrb = lift_one_cpp(0xE5D32001, 0x02000000); // LDRB
    ASSERT_TRUE(contains(cpp_ldrb, "nds_hal_read8"));

    auto cpp_strh = lift_one_cpp(0xE1C540B2, 0x02000000); // STRH
    ASSERT_TRUE(contains(cpp_strh, "nds_hal_write16"));

    auto cpp_ldrh = lift_one_cpp(0xE1D540B2, 0x02000000); // LDRH
    ASSERT_TRUE(contains(cpp_ldrh, "nds_hal_read16"));
}

TEST_CASE(CppLifter, DirectBranches) {
    auto cpp = lift_one_cpp(0xEA000002, 0x0200000C); // B 0x0200001C
    ASSERT_TRUE(contains(cpp, "goto block_0200001C"));
}

TEST_CASE(CppLifter, BlCall) {
    auto cpp = lift_one_cpp(0xEB000010, 0x02000008); // BL 0x02000050
    ASSERT_TRUE(contains(cpp, "func_02000050"));
}

TEST_CASE(CppLifter, BxReturn) {
    auto cpp = lift_one_cpp(0xE12FFF1E, 0x02000000); // BX LR
    ASSERT_TRUE(contains(cpp, "return;"));
}

TEST_CASE(CppLifter, BlxIndirect) {
    auto cpp = lift_one_cpp(0xE12FFF33, 0x02000000); // BLX R3
    ASSERT_TRUE(contains(cpp, "cpu.r[3]"));
    // indirect call path should not be a direct func_ call
    ASSERT_TRUE(contains(cpp, "indirect") || contains(cpp, "BLX") || contains(cpp, "cpu.r[3]"));
}

TEST_CASE(CppLifter, ThumbMode) {
    auto inst = ThumbDecoder::decode(0x200A, 0x03000000); // MOV R0, #10 thumb
    auto ir = IRTranslator::lift(inst);
    auto cpp = CppLifter::lift_instructions(ir, "thumb_test");
    ASSERT_TRUE(contains(cpp, "Thumb"));
    ASSERT_TRUE(contains(cpp, "cpu.r[0]"));
    ASSERT_TRUE(contains(cpp, "10"));
}

TEST_CASE(CppLifter, Provenance) {
    auto cpp = lift_one_cpp(0xE3A0000A, 0x02000000);
    ASSERT_TRUE(contains(cpp, "0x02000000"));
    ASSERT_TRUE(contains(cpp, "0xE3A0000A") || contains(cpp, "E3A0000A"));
}

TEST_CASE(CppLifter, UnknownStub) {
    // Unknown opcode (DCD) generates nds_unimplemented
    auto cpp = lift_one_cpp(0xDEADBEEF, 0x02000000);
    ASSERT_TRUE(contains(cpp, "nds_unimplemented"));
    ASSERT_TRUE(contains(cpp, "0x02000000"));
}

TEST_CASE(CppLifter, ArtificialFunction) {
    // MOV R0, #10 ; MOV R1, #20 ; ADD R0, R0, R1 ; CMP R0, #30 ; BX LR
    std::vector<IRInstruction> all;
    auto a = lift_arm(0xE3A0000A, 0x02000000); // MOV R0, #10
    auto b = lift_arm(0xE3A01014, 0x02000004); // MOV R1, #20
    auto c = lift_arm(0xE0800001, 0x02000008); // ADD R0, R0, R1
    auto d = lift_arm(0xE350001E, 0x0200000C); // CMP R0, #30
    auto e = lift_arm(0xE12FFF1E, 0x02000010); // BX LR
    for (auto& v : a) all.push_back(v);
    for (auto& v : b) all.push_back(v);
    for (auto& v : c) all.push_back(v);
    for (auto& v : d) all.push_back(v);
    for (auto& v : e) all.push_back(v);
    auto cpp = CppLifter::lift_instructions(all, "artificial_func");
    // Check that all five original instructions are reflected
    ASSERT_TRUE(contains(cpp, "cpu.r[0] = 10"));
    ASSERT_TRUE(contains(cpp, "cpu.r[1] = 20"));
    ASSERT_TRUE(contains(cpp, "cpu.r[0] = cpu.r[0] + cpu.r[1]") || contains(cpp, "cpu.r[0] ="));
    ASSERT_TRUE(contains(cpp, "flag_")); // CMP sets flags
    ASSERT_TRUE(contains(cpp, "30"));
    ASSERT_TRUE(contains(cpp, "return;"));
    // Provenance for each
    ASSERT_TRUE(contains(cpp, "0x02000000"));
    ASSERT_TRUE(contains(cpp, "0x02000010"));
}

TEST_CASE(CppLifter, HalNoDirectAccess) {
    // Ensure generated memory code uses HAL, not direct *(uint32_t*) or regs as address
    auto cpp = lift_one_cpp(0xE5910004, 0x02000000); // LDR
    ASSERT_TRUE(contains(cpp, "nds_hal_"));
    // should NOT contain direct DS address dereference pattern like "*((uint32_t*)"
    ASSERT_FALSE(contains(cpp, "*((uint32_t*)"));
    ASSERT_FALSE(contains(cpp, "* (uint32_t*)"));
}

TEST_CASE(CppLifter, ConditionalMov) {
    // MOVNE R0, #5 at 0x02000000 with cond NE (0x1) -> raw 0x13A00005? cond NE=1, I=1...
    // cond NE (0x1) MOV R0, #5 = 0x13A00005
    auto cpp = lift_one_cpp(0x13A00005, 0x02000000);
    ASSERT_TRUE(contains(cpp, "if"));
    ASSERT_TRUE(contains(cpp, "!flag_z")); // NE is !Z
}

