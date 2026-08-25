#include "test_framework.h"
#include "arm/arm_decoder.h"
#include <iostream>

using namespace descomp::arm;

TEST_CASE(ARMDecoder, MovAndAddImmediate) {
    // MOV R0, #0 -> 0xE3A00000
    auto inst1 = ARMDecoder::decode(0xE3A00000, 0x02000000);
    ASSERT_EQ(inst1.type, InstructionType::MOV);
    ASSERT_EQ(inst1.condition, Condition::AL);
    ASSERT_EQ(inst1.rd, Register::R0);
    ASSERT_TRUE(inst1.has_immediate);
    ASSERT_EQ(inst1.immediate, 0);

    // ADD R1, R0, #1 -> 0xE2801001
    auto inst2 = ARMDecoder::decode(0xE2801001, 0x02000004);
    ASSERT_EQ(inst2.type, InstructionType::ADD);
    ASSERT_EQ(inst2.rd, Register::R1);
    ASSERT_EQ(inst2.rn, Register::R0);
    ASSERT_TRUE(inst2.has_immediate);
    ASSERT_EQ(inst2.immediate, 1);
}

TEST_CASE(ARMDecoder, SubRsbAndLogic) {
    // SUB R2, R1, R0 -> 0xE0412000
    auto inst_sub = ARMDecoder::decode(0xE0412000, 0x02000000);
    ASSERT_EQ(inst_sub.type, InstructionType::SUB);
    ASSERT_EQ(inst_sub.rd, Register::R2);
    ASSERT_EQ(inst_sub.rn, Register::R1);
    ASSERT_EQ(inst_sub.rm, Register::R0);

    // RSB R3, R2, #10 -> 0xE262300A
    auto inst_rsb = ARMDecoder::decode(0xE262300A, 0x02000000);
    ASSERT_EQ(inst_rsb.type, InstructionType::RSB);
    ASSERT_EQ(inst_rsb.rd, Register::R3);
    ASSERT_EQ(inst_rsb.rn, Register::R2);
    ASSERT_EQ(inst_rsb.immediate, 10);

    // AND R4, R0, R1 -> 0xE0004001
    auto inst_and = ARMDecoder::decode(0xE0004001, 0x02000000);
    ASSERT_EQ(inst_and.type, InstructionType::AND);
    ASSERT_EQ(inst_and.rd, Register::R4);

    // ORR R5, R2, R3 -> 0xE1825003
    auto inst_orr = ARMDecoder::decode(0xE1825003, 0x02000000);
    ASSERT_EQ(inst_orr.type, InstructionType::ORR);

    // EOR R6, R4, R5 -> 0xE0246005
    auto inst_eor = ARMDecoder::decode(0xE0246005, 0x02000000);
    ASSERT_EQ(inst_eor.type, InstructionType::EOR);

    // BIC R7, R6, #0xFF -> 0xE3C670FF
    auto inst_bic = ARMDecoder::decode(0xE3C670FF, 0x02000000);
    ASSERT_EQ(inst_bic.type, InstructionType::BIC);

    // MVN R8, #0 -> 0xE3E08000
    auto inst_mvn = ARMDecoder::decode(0xE3E08000, 0x02000000);
    ASSERT_EQ(inst_mvn.type, InstructionType::MVN);
    ASSERT_EQ(inst_mvn.rd, Register::R8);
}

TEST_CASE(ARMDecoder, Comparisons) {
    // CMP R0, #5 -> 0xE3500005
    auto inst_cmp = ARMDecoder::decode(0xE3500005, 0x02000000);
    ASSERT_EQ(inst_cmp.type, InstructionType::CMP);
    ASSERT_EQ(inst_cmp.rn, Register::R0);
    ASSERT_EQ(inst_cmp.immediate, 5);

    // CMN R1, #10 -> 0xE371000A
    auto inst_cmn = ARMDecoder::decode(0xE371000A, 0x02000000);
    ASSERT_EQ(inst_cmn.type, InstructionType::CMN);

    // TST R2, #1 -> 0xE3120001
    auto inst_tst = ARMDecoder::decode(0xE3120001, 0x02000000);
    ASSERT_EQ(inst_tst.type, InstructionType::TST);

    // TEQ R3, R4 -> 0xE1330004
    auto inst_teq = ARMDecoder::decode(0xE1330004, 0x02000000);
    ASSERT_EQ(inst_teq.type, InstructionType::TEQ);
}

TEST_CASE(ARMDecoder, SingleAndHalfwordDataTransfer) {
    // LDR R0, [R1, #4] -> 0xE5910004
    auto inst_ldr = ARMDecoder::decode(0xE5910004, 0x02000000);
    ASSERT_EQ(inst_ldr.type, InstructionType::LDR);
    ASSERT_EQ(inst_ldr.rd, Register::R0);
    ASSERT_EQ(inst_ldr.rn, Register::R1);
    ASSERT_EQ(inst_ldr.immediate, 4);

    // STR R0, [R1, #4] -> 0xE5810004
    auto inst_str = ARMDecoder::decode(0xE5810004, 0x02000000);
    ASSERT_EQ(inst_str.type, InstructionType::STR);

    // LDRB R2, [R3, #1] -> 0xE5D32001
    auto inst_ldrb = ARMDecoder::decode(0xE5D32001, 0x02000000);
    ASSERT_EQ(inst_ldrb.type, InstructionType::LDRB);

    // STRB R2, [R3, #1] -> 0xE5C32001
    auto inst_strb = ARMDecoder::decode(0xE5C32001, 0x02000000);
    ASSERT_EQ(inst_strb.type, InstructionType::STRB);

    // LDRH R4, [R5, #2] -> 0xE1D540B2
    auto inst_ldrh = ARMDecoder::decode(0xE1D540B2, 0x02000000);
    ASSERT_EQ(inst_ldrh.type, InstructionType::LDRH);
    ASSERT_EQ(inst_ldrh.rd, Register::R4);
    ASSERT_EQ(inst_ldrh.rn, Register::R5);
    ASSERT_EQ(inst_ldrh.immediate, 2);

    // STRH R4, [R5, #2] -> 0xE1C540B2
    auto inst_strh = ARMDecoder::decode(0xE1C540B2, 0x02000000);
    ASSERT_EQ(inst_strh.type, InstructionType::STRH);
}

TEST_CASE(ARMDecoder, BranchAndCall) {
    // B 0x0200001C from 0x02000000 -> 0x02000000 + 8 + 0x5 * 4 = 0x02000020
    // At 0x0200000C, B 0x0200001C -> offset = (0x0200001C - (0x0200000C + 8)) / 4 = (0x1C - 0x14)/4 = 8/4 = 2 -> 0xEA000002
    auto inst_b = ARMDecoder::decode(0xEA000002, 0x0200000C);
    ASSERT_EQ(inst_b.type, InstructionType::B);
    ASSERT_TRUE(inst_b.is_branch);
    ASSERT_FALSE(inst_b.is_conditional_branch);
    ASSERT_EQ(inst_b.branch_target, 0x0200001CU);

    // BL 0x02000050 at 0x02000008 -> offset = (0x50 - 0x10) / 4 = 0x40 / 4 = 0x10 -> 0xEB000010
    auto inst_bl = ARMDecoder::decode(0xEB000010, 0x02000008);
    ASSERT_EQ(inst_bl.type, InstructionType::BL);
    ASSERT_TRUE(inst_bl.is_branch);
    ASSERT_TRUE(inst_bl.is_call);
    ASSERT_EQ(inst_bl.branch_target, 0x02000050U);

    // BX LR -> 0xE12FFF1E
    auto inst_bx = ARMDecoder::decode(0xE12FFF1E, 0x02000000);
    ASSERT_EQ(inst_bx.type, InstructionType::BX);
    ASSERT_TRUE(inst_bx.is_branch);
    ASSERT_TRUE(inst_bx.is_return);

    // BLX R3 -> 0xE12FFF33
    auto inst_blx = ARMDecoder::decode(0xE12FFF33, 0x02000000);
    ASSERT_EQ(inst_blx.type, InstructionType::BLX);
    ASSERT_TRUE(inst_blx.is_call);
    ASSERT_TRUE(inst_blx.is_indirect_branch);
}

TEST_CASE(ARMDecoder, StackAndBlockTransfer) {
    // PUSH {R4, R5, LR} -> STMDB SP!, {R4, R5, LR} -> 0xE92D4030
    auto inst_push = ARMDecoder::decode(0xE92D4030, 0x02000000);
    ASSERT_EQ(inst_push.type, InstructionType::PUSH);
    ASSERT_EQ(inst_push.register_list, (1 << 4) | (1 << 5) | (1 << 14));

    // POP {R4, R5, PC} -> LDMIA SP!, {R4, R5, PC} -> 0xE8BD8030
    auto inst_pop = ARMDecoder::decode(0xE8BD8030, 0x02000000);
    ASSERT_EQ(inst_pop.type, InstructionType::POP);
    ASSERT_TRUE(inst_pop.is_return);
    ASSERT_EQ(inst_pop.register_list, (1 << 4) | (1 << 5) | (1 << 15));
}

TEST_CASE(ARMDecoder, MultiplyAndCLZ) {
    // MUL R0, R1, R2 -> 0xE0000291
    auto inst_mul = ARMDecoder::decode(0xE0000291, 0x02000000);
    ASSERT_EQ(inst_mul.type, InstructionType::MUL);
    ASSERT_EQ(inst_mul.rd, Register::R0);
    ASSERT_EQ(inst_mul.rm, Register::R1);
    ASSERT_EQ(inst_mul.rs, Register::R2);

    // MLA R0, R1, R2, R3 -> 0xE0203291
    auto inst_mla = ARMDecoder::decode(0xE0203291, 0x02000000);
    ASSERT_EQ(inst_mla.type, InstructionType::MLA);

    // CLZ R0, R1 -> 0xE16F0F11
    auto inst_clz = ARMDecoder::decode(0xE16F0F11, 0x02000000);
    ASSERT_EQ(inst_clz.type, InstructionType::CLZ);
    ASSERT_EQ(inst_clz.rd, Register::R0);
    ASSERT_EQ(inst_clz.rm, Register::R1);
}

TEST_CASE(ARMDecoder, ConditionCodes) {
    // BEQ 0x02000020 at 0x02000000 -> 0x0A000006
    auto inst_beq = ARMDecoder::decode(0x0A000006, 0x02000000);
    ASSERT_EQ(inst_beq.type, InstructionType::B);
    ASSERT_EQ(inst_beq.condition, Condition::EQ);
    ASSERT_TRUE(inst_beq.is_conditional_branch);

    // BNE -> 0x1A000006
    auto inst_bne = ARMDecoder::decode(0x1A000006, 0x02000000);
    ASSERT_EQ(inst_bne.condition, Condition::NE);

    // BGT -> 0xCA000006
    auto inst_bgt = ARMDecoder::decode(0xCA000006, 0x02000000);
    ASSERT_EQ(inst_bgt.condition, Condition::GT);

    // BLE -> 0xDA000006
    auto inst_ble = ARMDecoder::decode(0xDA000006, 0x02000000);
    ASSERT_EQ(inst_ble.condition, Condition::LE);
}
