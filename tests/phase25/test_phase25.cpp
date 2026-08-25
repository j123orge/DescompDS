#include "test_framework.h"
#include "ir/ir_translator.h"
#include "ir/ir_value.h"
#include "lifter/cpp_lifter.h"
#include "arm/arm_decoder.h"
#include "arm/thumb_decoder.h"
#include "arm/arm_instruction.h"

using namespace descomp::ir;
using namespace descomp::arm;
using namespace descomp::lifter;

static std::vector<IRInstruction> lift_arm(uint32_t raw, uint32_t addr) {
    return IRTranslator::lift(ARMDecoder::decode(raw, addr));
}
static std::string lift_cpp(const std::vector<IRInstruction>& ir, const char* name="test") {
    return CppLifter::lift_instructions(ir, name);
}
static bool has(const std::string& s, const std::string& sub){ return s.find(sub)!=std::string::npos; }

// ---------------------------------------------------------------------------
// Prioridade 1: LDM/STM/PUSH/POP
// ---------------------------------------------------------------------------
TEST_CASE(Phase25_IR, PushDecomposes) {
    // PUSH {R4,R5,LR} -> 0xE92D4030
    auto ir = lift_arm(0xE92D4030, 0x02000000);
    // Expect 3 stores + 1 SP update = 4 (no UNKNOWN)
    ASSERT_EQ(ir.size(), 4U);
    for (auto &i: ir) ASSERT_NE(i.op, IROperation::UNKNOWN);
    // All except the SP update should be STORE
    int stores=0, adds=0;
    for (auto &i: ir){ if(i.op==IROperation::STORE) stores++; if(i.op==IROperation::ADD||i.op==IROperation::SUB) adds++; }
    ASSERT_EQ(stores, 3);
    ASSERT_EQ(adds, 1);
    // Check that no nds_unimplemented in C++
    auto cpp = lift_cpp(ir);
    ASSERT_TRUE(has(cpp, "nds_hal_write32"));
    ASSERT_FALSE(has(cpp, "nds_unimplemented(0x"));
    ASSERT_TRUE(has(cpp, "cpu.r[13]")); // SP
}

TEST_CASE(Phase25_IR, PopDecomposesWithPC) {
    // POP {R4,R5,PC} -> 0xE8BD8030
    auto ir = lift_arm(0xE8BD8030, 0x02000000);
    // 2 non-PC loads + 1 SP update + 1 PC load + 1 RETURN = 5
    ASSERT_TRUE(ir.size() >= 4);
    bool has_load=false, has_ret=false;
    for (auto &i: ir){ if(i.op==IROperation::LOAD) has_load=true; if(i.op==IROperation::RETURN) has_ret=true; ASSERT_NE(i.op, IROperation::UNKNOWN); }
    ASSERT_TRUE(has_load);
    ASSERT_TRUE(has_ret);
    auto cpp = lift_cpp(ir);
    ASSERT_TRUE(has(cpp, "nds_hal_read32"));
    ASSERT_TRUE(has(cpp, "return;"));
    ASSERT_FALSE(has(cpp, "nds_unimplemented(0x"));
}

TEST_CASE(Phase25_IR, LdmiaDecomposes) {
    // LDMIA R0, {R1,R2} -> 0xE8900006 (no writeback, IA)
    auto ir = lift_arm(0xE8900006, 0x02001000);
    ASSERT_EQ(ir.size(), 2U);
    for (auto &i: ir) ASSERT_EQ(i.op, IROperation::LOAD);
    auto cpp = lift_cpp(ir);
    ASSERT_TRUE(has(cpp, "nds_hal_read32"));
    ASSERT_TRUE(has(cpp, "cpu.r[0]")); // base
}

TEST_CASE(Phase25_IR, ThumbPushPop) {
    // Thumb PUSH {r0,r1,lr} -> 0xB403
    auto inst_push = ThumbDecoder::decode(0xB403, 0x03000000);
    auto ir_push = IRTranslator::lift(inst_push);
    ASSERT_TRUE(ir_push.size() >= 3);
    for (auto &i: ir_push) ASSERT_NE(i.op, IROperation::UNKNOWN);
    auto cpp_push = CppLifter::lift_instructions(ir_push, "thumb_push");
    ASSERT_TRUE(has(cpp_push, "nds_hal_write32"));

    // Thumb POP {r0,r1,pc} -> 0xBD03
    auto inst_pop = ThumbDecoder::decode(0xBD03, 0x03000000);
    auto ir_pop = IRTranslator::lift(inst_pop);
    ASSERT_TRUE(ir_pop.size() >= 3);
    bool has_ret=false;
    for (auto &i: ir_pop) if(i.op==IROperation::RETURN) has_ret=true;
    ASSERT_TRUE(has_ret);
    auto cpp_pop = CppLifter::lift_instructions(ir_pop, "thumb_pop");
    ASSERT_TRUE(has(cpp_pop, "return;"));
}

// ---------------------------------------------------------------------------
// Prioridade 2: CLZ
// ---------------------------------------------------------------------------
TEST_CASE(Phase25_IR, Clz) {
    // CLZ R0, R1 -> 0xE16F0F11
    auto ir = lift_arm(0xE16F0F11, 0x02000000);
    ASSERT_EQ(ir.size(), 1U);
    ASSERT_EQ(ir[0].op, IROperation::CLZ);
    ASSERT_EQ(ir[0].dest->id, 0U);
    ASSERT_EQ(ir[0].operands[0], IRValue::reg(1));
    auto cpp = lift_cpp(ir);
    ASSERT_TRUE(has(cpp, "nds_clz"));
    ASSERT_FALSE(has(cpp, "nds_unimplemented(0x"));
}

// ---------------------------------------------------------------------------
// Prioridade 3: MRS/MSR
// ---------------------------------------------------------------------------
TEST_CASE(Phase25_IR, MrsCpsr) {
    // MRS R0, CPSR -> 0xE10F0000
    auto ir = lift_arm(0xE10F0000, 0x02000000);
    ASSERT_EQ(ir[0].op, IROperation::MRS);
    ASSERT_EQ(ir[0].dest->id, 0U);
    ASSERT_FALSE(ir[0].spsr);
    auto cpp = lift_cpp(ir);
    ASSERT_TRUE(has(cpp, "flag_n"));
    ASSERT_FALSE(has(cpp, "nds_unimplemented(0x"));
}
TEST_CASE(Phase25_IR, MrsSpsr) {
    // MRS R0, SPSR -> 0xE14F0000 (bit22 set)
    auto ir = lift_arm(0xE14F0000, 0x02000000);
    ASSERT_EQ(ir[0].op, IROperation::MRS);
    ASSERT_TRUE(ir[0].spsr);
}
TEST_CASE(Phase25_IR, MsrCpsr) {
    // MSR CPSR, R1 -> 0xE129F001 (should decode as MSR)
    // Use manual construction to avoid raw uncertainty: build ARMInstruction directly
    ARMInstruction inst;
    inst.type = InstructionType::MSR;
    inst.rd = REG_NONE;
    inst.rm = 1;
    inst.spsr = false;
    inst.msr_mask = 0x8;
    inst.address = 0x02000000;
    inst.raw = 0xE129F001;
    inst.mnemonic = "MSR";
    auto ir = IRTranslator::lift(inst);
    ASSERT_EQ(ir[0].op, IROperation::MSR);
    ASSERT_FALSE(ir[0].spsr);
    auto cpp = lift_cpp(ir);
    ASSERT_TRUE(has(cpp, "nds_msr_cpsr_flags") || has(cpp, "flag_n"));
}
TEST_CASE(Phase25_IR, MsrImmediate) {
    ARMInstruction inst;
    inst.type = InstructionType::MSR;
    inst.spsr = false;
    inst.msr_mask = 0x8;
    inst.has_immediate = true;
    inst.immediate = 0xFF;
    inst.address = 0x02000000;
    auto ir = IRTranslator::lift(inst);
    ASSERT_EQ(ir[0].op, IROperation::MSR);
    auto cpp = lift_cpp(ir);
    ASSERT_TRUE(has(cpp, "nds_msr_cpsr_flags"));
}

// ---------------------------------------------------------------------------
// Prioridade 4: SWI/BKPT
// ---------------------------------------------------------------------------
TEST_CASE(Phase25_IR, Swi) {
    auto ir = lift_arm(0xEF000000, 0x02000000); // SWI #0 ARM
    ASSERT_EQ(ir[0].op, IROperation::SWI);
    auto cpp = lift_cpp(ir);
    ASSERT_TRUE(has(cpp, "nds_swi"));
    ASSERT_FALSE(has(cpp, "nds_unimplemented(0x"));
    // Thumb SWI #0 -> 0xDF00
    auto t = ThumbDecoder::decode(0xDF00, 0x03000000);
    auto ir2 = IRTranslator::lift(t);
    ASSERT_EQ(ir2[0].op, IROperation::SWI);
}
TEST_CASE(Phase25_IR, Bkpt) {
    // BKPT #0 -> 0xE1200070
    auto ir = lift_arm(0xE1200070, 0x02000000);
    ASSERT_EQ(ir[0].op, IROperation::BKPT);
    auto cpp = lift_cpp(ir);
    ASSERT_TRUE(has(cpp, "nds_bkpt"));
}

// ---------------------------------------------------------------------------
// Prioridade 5: Flags N/Z/C/V
// ---------------------------------------------------------------------------
TEST_CASE(Phase25_Flags, AddFlags) {
    // ADD R0, R1, R2, S -> 0xE0910002
    auto ir = lift_arm(0xE0910002, 0x02000000);
    ASSERT_TRUE(ir[0].update_flags);
    ASSERT_TRUE(has(lift_cpp(ir), "flag_c"));
    ASSERT_TRUE(has(lift_cpp(ir), "flag_v"));
    ASSERT_TRUE(has(lift_cpp(ir), "flag_n"));
}
TEST_CASE(Phase25_Flags, AdcFlags) {
    // ADC R0, R1, R2, S -> opcode 5, S=1
    // Encode: 0xE0B10002 (ADC R0,R1,R2)
    auto ir = lift_arm(0xE0B10002, 0x02000000);
    ASSERT_EQ(ir[0].op, IROperation::ADC);
    ASSERT_TRUE(ir[0].update_flags);
    auto cpp = lift_cpp(ir);
    ASSERT_TRUE(has(cpp, "flag_c"));
    ASSERT_TRUE(has(cpp, "_c_in"));
}
TEST_CASE(Phase25_Flags, SubFlags) {
    auto ir = lift_arm(0xE0510002, 0x02000000); // SUB R0,R1,R2,S -> 0xE0510002
    ASSERT_EQ(ir[0].op, IROperation::SUB);
    auto cpp = lift_cpp(ir);
    ASSERT_TRUE(has(cpp, "flag_c"));
    ASSERT_TRUE(has(cpp, "flag_v"));
}
TEST_CASE(Phase25_Flags, CmpFlags) {
    auto ir = lift_arm(0xE3500005, 0x02000000); // CMP
    auto cpp = lift_cpp(ir);
    ASSERT_TRUE(has(cpp, "flag_n"));
    ASSERT_TRUE(has(cpp, "flag_z"));
    ASSERT_TRUE(has(cpp, "flag_c"));
    ASSERT_TRUE(has(cpp, "flag_v"));
    ASSERT_TRUE(has(cpp, ">=")); // C = a >= b
}
TEST_CASE(Phase25_Flags, ThumbShiftFlags) {
    // Thumb LSL R0,R1,#2 -> 0x0088? Format 1: LSL R0,R1,#2 => raw 0x0088 (0000 00 10 001 000)
    auto t = ThumbDecoder::decode(0x0088, 0x03000000);
    auto ir = IRTranslator::lift(t);
    ASSERT_EQ(ir[0].op, IROperation::LSL);
    auto cpp = CppLifter::lift_instructions(ir, "tshift");
    ASSERT_TRUE(has(cpp, "flag_n"));
    ASSERT_TRUE(has(cpp, "flag_c"));
}


