#include "test_framework.h"
#include "arm/thumb_decoder.h"
#include <iostream>

using namespace descomp::arm;

TEST_CASE(ThumbDecoder, MovAddSubCmp) {
    // MOV R0, #10 -> 0x200A
    auto inst_mov = ThumbDecoder::decode(0x200A, 0x02000000);
    ASSERT_EQ(inst_mov.type, InstructionType::MOV);
    ASSERT_EQ(inst_mov.rd, Register::R0);
    ASSERT_EQ(inst_mov.immediate, 10);
    ASSERT_TRUE(inst_mov.is_thumb);

    // ADD R1, #5 -> 0x3105
    auto inst_add = ThumbDecoder::decode(0x3105, 0x02000002);
    ASSERT_EQ(inst_add.type, InstructionType::ADD);
    ASSERT_EQ(inst_add.rd, Register::R1);
    ASSERT_EQ(inst_add.immediate, 5);

    // SUB R2, #3 -> 0x3A03
    auto inst_sub = ThumbDecoder::decode(0x3A03, 0x02000004);
    ASSERT_EQ(inst_sub.type, InstructionType::SUB);
    ASSERT_EQ(inst_sub.rd, Register::R2);
    ASSERT_EQ(inst_sub.immediate, 3);

    // CMP R0, #5 -> 0x2805
    auto inst_cmp = ThumbDecoder::decode(0x2805, 0x02000006);
    ASSERT_EQ(inst_cmp.type, InstructionType::CMP);
    ASSERT_EQ(inst_cmp.rd, Register::R0);
    ASSERT_EQ(inst_cmp.immediate, 5);
}

TEST_CASE(ThumbDecoder, LogicAndShifts) {
    // AND R0, R1 -> 0x4008
    auto inst_and = ThumbDecoder::decode(0x4008, 0x02000000);
    ASSERT_EQ(inst_and.type, InstructionType::AND);
    ASSERT_EQ(inst_and.rd, Register::R0);
    ASSERT_EQ(inst_and.rm, Register::R1);

    // EOR R2, R3 -> 0x405A
    auto inst_eor = ThumbDecoder::decode(0x405A, 0x02000002);
    ASSERT_EQ(inst_eor.type, InstructionType::EOR);

    // ORR R4, R5 -> 0x432C
    auto inst_orr = ThumbDecoder::decode(0x432C, 0x02000004);
    ASSERT_EQ(inst_orr.type, InstructionType::ORR);

    // BIC R6, R7 -> 0x43BE
    auto inst_bic = ThumbDecoder::decode(0x43BE, 0x02000006);
    ASSERT_EQ(inst_bic.type, InstructionType::BIC);

    // MUL R0, R1 -> 0x4348
    auto inst_mul = ThumbDecoder::decode(0x4348, 0x02000008);
    ASSERT_EQ(inst_mul.type, InstructionType::MUL);

    // LSL R0, R1, #2 -> 0x0088
    auto inst_lsl = ThumbDecoder::decode(0x0088, 0x0200000A);
    ASSERT_EQ(inst_lsl.type, InstructionType::LSL);
    ASSERT_EQ(inst_lsl.shift_amount, 2);

    // LSR R2, R3, #3 -> 0x08DA
    auto inst_lsr = ThumbDecoder::decode(0x08DA, 0x0200000C);
    ASSERT_EQ(inst_lsr.type, InstructionType::LSR);
    ASSERT_EQ(inst_lsr.shift_amount, 3);

    // ASR R4, R5, #4 -> 0x112C
    auto inst_asr = ThumbDecoder::decode(0x112C, 0x0200000E);
    ASSERT_EQ(inst_asr.type, InstructionType::ASR);
    ASSERT_EQ(inst_asr.shift_amount, 4);
}

TEST_CASE(ThumbDecoder, LoadStoreAndStack) {
    // LDR R0, [R1, #4] -> 0x6848 (offset 1 * 4 = 4)
    auto inst_ldr = ThumbDecoder::decode(0x6848, 0x02000000);
    ASSERT_EQ(inst_ldr.type, InstructionType::LDR);
    ASSERT_EQ(inst_ldr.rd, Register::R0);
    ASSERT_EQ(inst_ldr.rn, Register::R1);
    ASSERT_EQ(inst_ldr.immediate, 4);

    // STR R0, [R1, #4] -> 0x6048
    auto inst_str = ThumbDecoder::decode(0x6048, 0x02000002);
    ASSERT_EQ(inst_str.type, InstructionType::STR);

    // LDRB R2, [R3, #1] -> 0x785A
    auto inst_ldrb = ThumbDecoder::decode(0x785A, 0x02000004);
    ASSERT_EQ(inst_ldrb.type, InstructionType::LDRB);

    // STRB R2, [R3, #1] -> 0x705A
    auto inst_strb = ThumbDecoder::decode(0x705A, 0x02000006);
    ASSERT_EQ(inst_strb.type, InstructionType::STRB);

    // LDRH R4, [R5, #2] -> 0x886C (offset 1 * 2 = 2)
    auto inst_ldrh = ThumbDecoder::decode(0x886C, 0x02000008);
    ASSERT_EQ(inst_ldrh.type, InstructionType::LDRH);

    // STRH R4, [R5, #2] -> 0x806C
    auto inst_strh = ThumbDecoder::decode(0x806C, 0x0200000A);
    ASSERT_EQ(inst_strh.type, InstructionType::STRH);

    // PUSH {R4, R5, LR} -> 0xB530
    auto inst_push = ThumbDecoder::decode(0xB530, 0x0200000C);
    ASSERT_EQ(inst_push.type, InstructionType::PUSH);
    ASSERT_EQ(inst_push.register_list, (1 << 4) | (1 << 5) | (1 << 14));

    // POP {R4, R5, PC} -> 0xBD30
    auto inst_pop = ThumbDecoder::decode(0xBD30, 0x0200000E);
    ASSERT_EQ(inst_pop.type, InstructionType::POP);
    ASSERT_TRUE(inst_pop.is_return);
    ASSERT_EQ(inst_pop.register_list, (1 << 4) | (1 << 5) | (1 << 15));
}

TEST_CASE(ThumbDecoder, BranchesAndBL) {
    // BX LR -> 0x4770
    auto inst_bx = ThumbDecoder::decode(0x4770, 0x02000000);
    ASSERT_EQ(inst_bx.type, InstructionType::BX);
    ASSERT_TRUE(inst_bx.is_return);

    // B 0x02000020 from 0x02000000 -> target = 0x02000000 + 4 + offset -> offset = 0x1C = 28 -> imm11 = 14 = 0x0E -> 0xE00E
    auto inst_b = ThumbDecoder::decode(0xE00E, 0x02000000);
    ASSERT_EQ(inst_b.type, InstructionType::B);
    ASSERT_TRUE(inst_b.is_branch);
    ASSERT_FALSE(inst_b.is_conditional_branch);
    ASSERT_EQ(inst_b.branch_target, 0x02000020U); // 0 + 4 + 14*2 = 32 = 0x20

    // BEQ 0x02000010 from 0x02000000 -> 0xD006 (0 + 4 + 6*2 = 16 = 0x10)
    auto inst_beq = ThumbDecoder::decode(0xD006, 0x02000000);
    ASSERT_EQ(inst_beq.type, InstructionType::B);
    ASSERT_EQ(inst_beq.condition, Condition::EQ);
    ASSERT_TRUE(inst_beq.is_conditional_branch);
    ASSERT_EQ(inst_beq.branch_target, 0x02000010U);

    // Long BL: 0xF000 0xF808 from 0x02000000 -> target = 0x02000000 + 4 + 0x10 = 0x02000014
    auto inst_bl = ThumbDecoder::decode(0xF000, 0x02000000, 0xF808);
    ASSERT_EQ(inst_bl.type, InstructionType::BL);
    ASSERT_TRUE(inst_bl.is_call);
    ASSERT_EQ(inst_bl.branch_target, 0x02000014U);
}
