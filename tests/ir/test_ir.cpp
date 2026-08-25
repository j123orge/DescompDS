#include "test_framework.h"
#include "ir/ir_translator.h"
#include "ir/ir_value.h"
#include "ir/ir_instruction.h"
#include "arm/arm_decoder.h"
#include "arm/thumb_decoder.h"

using namespace descomp::ir;
using namespace descomp::arm;

// Helper: decode a single ARM instruction and lower it.
static std::vector<IRInstruction> lift_arm(uint32_t raw, uint32_t addr) {
    return IRTranslator::lift(ARMDecoder::decode(raw, addr));
}

static IRInstruction lift_arm_single(uint32_t raw, uint32_t addr) {
    auto v = lift_arm(raw, addr);
    ASSERT_EQ(v.size(), 1U);
    return v[0];
}

TEST_CASE(IR, Mov) {
    // MOV R0, #0  -> 0xE3A00000
    auto ir = lift_arm_single(0xE3A00000, 0x02000000);
    ASSERT_EQ(ir.op, IROperation::MOV);
    ASSERT_TRUE(ir.dest.has_value());
    ASSERT_EQ(ir.dest->kind, IRValueKind::Register);
    ASSERT_EQ(ir.dest->id, static_cast<uint32_t>(Register::R0));
    ASSERT_EQ(ir.operands.size(), 1U);
    ASSERT_EQ(ir.operands[0].kind, IRValueKind::Immediate);
    ASSERT_EQ(ir.operands[0].immediate, 0);
    ASSERT_FALSE(ir.update_flags);
}

TEST_CASE(IR, Add) {
    // ADD R1, R0, #1  -> 0xE2801001
    auto ir = lift_arm_single(0xE2801001, 0x02000004);
    ASSERT_EQ(ir.op, IROperation::ADD);
    ASSERT_EQ(ir.dest->id, static_cast<uint32_t>(Register::R1));
    ASSERT_EQ(ir.operands.size(), 2U);
    ASSERT_EQ(ir.operands[0], IRValue::reg(Register::R0));
    ASSERT_EQ(ir.operands[1], IRValue::imm(1));
}

TEST_CASE(IR, Sub) {
    // SUB R2, R1, R0  -> 0xE0412000
    auto ir = lift_arm_single(0xE0412000, 0x02000000);
    ASSERT_EQ(ir.op, IROperation::SUB);
    ASSERT_EQ(ir.dest->id, static_cast<uint32_t>(Register::R2));
    ASSERT_EQ(ir.operands[0], IRValue::reg(Register::R1));
    ASSERT_EQ(ir.operands[1], IRValue::reg(Register::R0));
}

TEST_CASE(IR, Cmp) {
    // CMP R0, #5  -> 0xE3500005
    auto ir = lift_arm_single(0xE3500005, 0x02000000);
    ASSERT_EQ(ir.op, IROperation::CMP);
    ASSERT_FALSE(ir.dest.has_value());           // CMP writes no register
    ASSERT_TRUE(ir.update_flags);                // CMP always updates flags
    ASSERT_EQ(ir.affected_flags, FLAG_ALL);
    ASSERT_EQ(ir.operands[0], IRValue::reg(Register::R0));
    ASSERT_EQ(ir.operands[1], IRValue::imm(5));
}

TEST_CASE(IR, Ldr) {
    // LDR R0, [R1, #4]  -> 0xE5910004
    auto ir = lift_arm_single(0xE5910004, 0x02000000);
    ASSERT_EQ(ir.op, IROperation::LOAD);
    ASSERT_EQ(ir.dest->id, static_cast<uint32_t>(Register::R0));
    ASSERT_EQ(ir.operands.size(), 1U);
    ASSERT_EQ(ir.operands[0].kind, IRValueKind::Memory);
    const auto& m = ir.operands[0].memory;
    ASSERT_EQ(m.base_reg, static_cast<uint8_t>(Register::R1));
    ASSERT_EQ(m.displacement, 4);
    ASSERT_EQ(m.access, IRMemoryAccess::Word);
}

TEST_CASE(IR, Str) {
    // STR R0, [R1, #4]  -> 0xE5810004
    auto ir = lift_arm_single(0xE5810004, 0x02000000);
    ASSERT_EQ(ir.op, IROperation::STORE);
    ASSERT_EQ(ir.operands.size(), 1U);
    ASSERT_EQ(ir.operands[0], IRValue::reg(Register::R0));
    ASSERT_EQ(ir.dest->kind, IRValueKind::Memory);
    ASSERT_EQ(ir.dest->memory.base_reg, static_cast<uint8_t>(Register::R1));
    ASSERT_EQ(ir.dest->memory.displacement, 4);
}

TEST_CASE(IR, Branch) {
    // B 0x0200001C from 0x0200000C -> 0xEA000002
    auto ir = lift_arm_single(0xEA000002, 0x0200000C);
    ASSERT_EQ(ir.op, IROperation::BRANCH);
    ASSERT_TRUE(ir.is_control_flow);
    ASSERT_TRUE(ir.is_terminator);
    ASSERT_FALSE(ir.is_indirect);
    ASSERT_EQ(ir.operands.size(), 1U);
    ASSERT_EQ(ir.operands[0].kind, IRValueKind::Immediate);
    ASSERT_EQ(ir.operands[0].immediate, static_cast<int64_t>(0x0200001CU));
}

TEST_CASE(IR, ConditionalBranch) {
    // BEQ 0x02000020 at 0x02000000 -> 0x0A000006
    auto ir = lift_arm_single(0x0A000006, 0x02000000);
    ASSERT_EQ(ir.op, IROperation::COND_BRANCH);
    ASSERT_TRUE(ir.condition.has_value());
    ASSERT_EQ(*ir.condition, Condition::EQ);
    ASSERT_TRUE(ir.is_control_flow);
    ASSERT_TRUE(ir.is_terminator);
    ASSERT_EQ(ir.operands[0].immediate, static_cast<int64_t>(0x02000020U));
}

TEST_CASE(IR, Bl) {
    // BL 0x02000050 at 0x02000008 -> 0xEB000010
    auto ir = lift_arm_single(0xEB000010, 0x02000008);
    ASSERT_EQ(ir.op, IROperation::CALL);
    ASSERT_TRUE(ir.is_call);
    ASSERT_TRUE(ir.is_control_flow);
    ASSERT_FALSE(ir.is_terminator);   // calls return to the fallthrough
    ASSERT_FALSE(ir.is_indirect);
    ASSERT_EQ(ir.operands[0].immediate, static_cast<int64_t>(0x02000050U));
}

TEST_CASE(IR, Bx) {
    // BX LR  -> 0xE12FFF1E
    auto ir = lift_arm_single(0xE12FFF1E, 0x02000000);
    ASSERT_EQ(ir.op, IROperation::RETURN);
    ASSERT_TRUE(ir.is_terminator);
    ASSERT_FALSE(ir.is_indirect);
    ASSERT_EQ(ir.operands[0], IRValue::reg(Register::LR));
}

TEST_CASE(IR, ArmToIrMapping) {
    // Each supported ARM instruction must lower to a non-UNKNOWN IR op.
    struct Case { uint32_t raw; IROperation expected; };
    Case cases[] = {
        {0xE3A00000, IROperation::MOV},  // MOV R0,#0
        {0xE2801001, IROperation::ADD},  // ADD R1,R0,#1
        {0xE0412000, IROperation::SUB},  // SUB R2,R1,R0
        {0xE3500005, IROperation::CMP},  // CMP R0,#5
        {0xE5910004, IROperation::LOAD}, // LDR R0,[R1,#4]
        {0xE5810004, IROperation::STORE},// STR R0,[R1,#4]
        {0xE0120001, IROperation::AND},  // AND R0,R1? (R0=R1 AND R1) sanity
        {0xE1A00001, IROperation::MOV},  // MOV R0,R1
    };
    for (const auto& c : cases) {
        auto ir = lift_arm_single(c.raw, 0x02000000);
        ASSERT_EQ(ir.op, c.expected);
        ASSERT_NE(ir.op, IROperation::UNKNOWN);
        ASSERT_EQ(ir.original_type, ARMDecoder::decode(c.raw, 0x02000000).type);
    }
}

TEST_CASE(IR, ThumbToIr) {
    // Thumb MOV R0, #1  -> 0x2001
    {
        auto inst = ThumbDecoder::decode(0x2001, 0x03000000);
        ASSERT_TRUE(inst.is_thumb);
        auto ir = IRTranslator::lift(inst);
        ASSERT_EQ(ir.size(), 1U);
        ASSERT_EQ(ir[0].op, IROperation::MOV);
        ASSERT_EQ(ir[0].dest->id, static_cast<uint32_t>(Register::R0));
        ASSERT_EQ(ir[0].operands[0], IRValue::imm(1));
        ASSERT_TRUE(ir[0].is_thumb);
        ASSERT_EQ(ir[0].size, 2);
    }
    // Thumb AND R0, R1  -> 0x4008 (format 4: R0 = R0 AND R1)
    {
        auto inst = ThumbDecoder::decode(0x4008, 0x03000000);
        auto ir = IRTranslator::lift(inst);
        ASSERT_EQ(ir[0].op, IROperation::AND);
        ASSERT_EQ(ir[0].dest->id, 0U);
        ASSERT_EQ(ir[0].operands.size(), 2U);
        ASSERT_EQ(ir[0].operands[0], IRValue::reg(0)); // Rd is the first source
        ASSERT_EQ(ir[0].operands[1], IRValue::reg(1));
    }
    // Thumb unconditional B (self)  -> 0xE000 at 0x03000000 => target 0x03000004
    {
        auto inst = ThumbDecoder::decode(0xE000, 0x03000000);
        auto ir = IRTranslator::lift(inst);
        ASSERT_EQ(ir[0].op, IROperation::BRANCH);
        ASSERT_EQ(ir[0].operands[0].immediate, static_cast<int64_t>(0x03000004U));
    }
    // Thumb conditional BEQ  -> 0xD000 at 0x03000000
    {
        auto inst = ThumbDecoder::decode(0xD000, 0x03000000);
        auto ir = IRTranslator::lift(inst);
        ASSERT_EQ(ir[0].op, IROperation::COND_BRANCH);
        ASSERT_EQ(*ir[0].condition, Condition::EQ);
    }
}

TEST_CASE(IR, Flags) {
    // ADD R0, R1, R2 with S (sets flags)  -> 0xE0910002
    auto ir = lift_arm_single(0xE0910002, 0x02000000);
    ASSERT_EQ(ir.op, IROperation::ADD);
    ASSERT_TRUE(ir.update_flags);
    ASSERT_EQ(ir.affected_flags, FLAG_ALL);
    ASSERT_TRUE(mask_has_flag(ir.affected_flags, IRFlag::N));
    ASSERT_TRUE(mask_has_flag(ir.affected_flags, IRFlag::Z));
    ASSERT_TRUE(mask_has_flag(ir.affected_flags, IRFlag::C));
    ASSERT_TRUE(mask_has_flag(ir.affected_flags, IRFlag::V));
}

TEST_CASE(IR, AddressPreservation) {
    uint32_t raw = 0xE2801001;
    uint32_t addr = 0x0234ABCD;
    auto inst = ARMDecoder::decode(raw, addr);
    auto ir = lift_arm_single(raw, addr);
    ASSERT_EQ(ir.address, addr);
    ASSERT_EQ(ir.raw, raw);
    ASSERT_EQ(ir.original_type, inst.type);
    ASSERT_EQ(ir.mnemonic, inst.mnemonic);
}

TEST_CASE(IR, ThumbModePreservation) {
    auto arm_inst = ARMDecoder::decode(0xE3A00000, 0x02000000);
    auto arm_ir = IRTranslator::lift(arm_inst);
    ASSERT_FALSE(arm_ir[0].is_thumb);
    ASSERT_EQ(arm_ir[0].size, 4);

    auto thumb_inst = ThumbDecoder::decode(0x2001, 0x03000000);
    auto thumb_ir = IRTranslator::lift(thumb_inst);
    ASSERT_TRUE(thumb_ir[0].is_thumb);
    ASSERT_EQ(thumb_ir[0].size, 2);
}

TEST_CASE(IR, ShiftedOperandExpandsToTemp) {
    // MOV R0, R1, LSL #1  -> 0xE1A00081
    auto ir = lift_arm(0xE1A00081, 0x02000000);
    ASSERT_EQ(ir.size(), 2U);                 // shift + mov
    ASSERT_EQ(ir[0].op, IROperation::LSL);    // shift materialized first
    ASSERT_EQ(ir[1].op, IROperation::MOV);
    ASSERT_EQ(ir[1].dest->id, static_cast<uint32_t>(Register::R0));
    ASSERT_EQ(ir[1].operands.size(), 1U);
    ASSERT_TRUE(ir[1].operands[0].is_temporary());
}
