#include "test_framework.h"
#include "runtime/nds_runtime.h"
#include "runtime/dynamic_code.h"
#include "runtime/subsystem_access.h"
#include "arm/arm_decoder.h"
#include "arm/thumb_decoder.h"
#include "ir/ir_translator.h"
#include "ir/ir_interpreter.h"
#include "lifter/cpp_lifter.h"

using namespace descomp;

TEST_CASE(CPUState, FlagsRoundTrip) {
    runtime::CPUState cpu{};
    cpu.set_flags(true, false, true, false);
    ASSERT_TRUE(cpu.flag_n());
    ASSERT_FALSE(cpu.flag_z());
    ASSERT_TRUE(cpu.flag_c());
    ASSERT_FALSE(cpu.flag_v());
}

TEST_CASE(CPUState, RegisterAccess) {
    runtime::CPUState cpu{};
    cpu.r[0] = 0xDEADBEEF;
    cpu.r[13] = 0x023FFF00;
    cpu.r[14] = 0x0200480C;
    cpu.r[15] = 0x02004800;
    ASSERT_EQ(cpu.r[0], 0xDEADBEEF);
    ASSERT_EQ(cpu.r[13], 0x023FFF00);
    ASSERT_EQ(cpu.r[14], 0x0200480C);
    ASSERT_EQ(cpu.r[15], 0x02004800);
}

TEST_CASE(CPUState, ThumbMode) {
    runtime::CPUState cpu{};
    ASSERT_FALSE(cpu.thumb);
    cpu.thumb = true;
    ASSERT_TRUE(cpu.thumb);
}

TEST_CASE(MRCMCR, DecoderRecognizesMRC) {
    // MRC P15, 0, R0, C1, C0, 0 = 0xEE110F10
    auto inst = arm::ARMDecoder::decode(0xEE110F10, 0x020049F0);
    ASSERT_EQ(inst.type, arm::InstructionType::MRC);
    ASSERT_EQ(inst.rd, 0);
    ASSERT_EQ(inst.crn, 1);
    ASSERT_EQ(inst.crm, 0);
    ASSERT_EQ(inst.cp_num, 15);
    ASSERT_EQ(inst.cp_op2, 0);
}

TEST_CASE(MRCMCR, DecoderRecognizesMCR) {
    // MCR P15, 0, R0, C7, C10, 4 = 0xEE070F9A (example)
    // Simplified: MCR P15, #0, R0, C1, C0, 0
    // Construct: cond=1110 1110 0000 crn=0001 rd=0000 cp=1111 op2=000 0 crm=0000
    // = 0xEE010F10
    auto inst = arm::ARMDecoder::decode(0xEE010F10, 0x02005000);
    ASSERT_EQ(inst.type, arm::InstructionType::MCR);
    ASSERT_EQ(inst.rd, 0);
    ASSERT_EQ(inst.crn, 1);
    ASSERT_EQ(inst.cp_num, 15);
}

TEST_CASE(MRCMCR, IRTranslatorHandlesMRC) {
    auto inst = arm::ARMDecoder::decode(0xEE110F10, 0x020049F0);
    auto ir_list = ir::IRTranslator::lift(inst);
    ASSERT_TRUE(!ir_list.empty());
    ASSERT_EQ(ir_list[0].op, ir::IROperation::MRC);
    ASSERT_TRUE(ir_list[0].dest.has_value());
    ASSERT_EQ(ir_list[0].dest->kind, ir::IRValueKind::Register);
    ASSERT_EQ(ir_list[0].dest->id, 0);
}

TEST_CASE(MRCMCR, IRTranslatorHandlesMCR) {
    auto inst = arm::ARMDecoder::decode(0xEE010F10, 0x02005000);
    auto ir_list = ir::IRTranslator::lift(inst);
    ASSERT_TRUE(!ir_list.empty());
    ASSERT_EQ(ir_list[0].op, ir::IROperation::MCR);
}

TEST_CASE(Runtime, MemoryReadWrite) {
    uint8_t data[256] = {};
    std::fill(data, data + 256, 0);
    runtime::init_memory(data, 256, 0x02000000);
    runtime::write32(0x02000000, 0xDEADBEEF);
    ASSERT_EQ(runtime::read32(0x02000000), 0xDEADBEEF);
    runtime::write16(0x02000004, 0xBEEF);
    ASSERT_EQ(runtime::read16(0x02000004), 0xBEEF);
    runtime::write8(0x02000006, 0x42);
    ASSERT_EQ(runtime::read8(0x02000006), 0x42);
    runtime::shutdown_memory();
}

TEST_CASE(Runtime, MRCReturnsCPUID) {
    uint32_t val = runtime::nds_mrc(15, 0, 0, 0);
    ASSERT_EQ(val, 0x41059461u); // ARM946E-S CPU ID
}

TEST_CASE(Interpreter, SimpleExecution) {
    // MOV R0, #42; BX LR
    // Build a minimal program manually
    ir::IRProgram prog;
    ir::IRFunction fn;
    fn.address = 0x02000000;
    fn.thumb = false;

    ir::IRBasicBlock entry;
    entry.start = 0x02000000;
    entry.end = 0x02000008;

    ir::IRInstruction mov;
    mov.op = ir::IROperation::MOV;
    mov.dest = ir::IRValue::reg(0);
    mov.operands = { ir::IRValue::imm(42) };
    mov.address = 0x02000000;

    ir::IRInstruction bxlr;
    bxlr.op = ir::IROperation::RETURN;
    bxlr.operands = { ir::IRValue::reg(14) };
    bxlr.address = 0x02000004;
    bxlr.is_terminator = true;

    entry.instructions.push_back(mov);
    entry.instructions.push_back(bxlr);
    entry.successors = {};
    fn.blocks.push_back(0x02000000);
    fn.block_map[0x02000000] = entry;
    prog.functions[0x02000000] = fn;

    runtime::CPUState cpu{};
    cpu.r[14] = 0x02000100;

    ir::IRInterpreter interp;
    interp.init(cpu);
    interp.set_program(prog);
    auto stats = interp.execute(fn, 100);

    ASSERT_TRUE(stats.stopped);
    ASSERT_EQ(stats.instructions_executed, 2u);
    ASSERT_EQ(cpu.r[0], 42u);
    ASSERT_EQ(cpu.r[15], 0x02000100u);
}

TEST_CASE(Interpreter, BranchExecution) {
    // MOV R0, #1; B target; target: MOV R0, #2; BX LR
    ir::IRProgram prog;
    ir::IRFunction fn;
    fn.address = 0x02000000;
    fn.thumb = false;

    ir::IRBasicBlock blk1;
    blk1.start = 0x02000000;
    blk1.end = 0x02000008;

    ir::IRInstruction mov1;
    mov1.op = ir::IROperation::MOV;
    mov1.dest = ir::IRValue::reg(0);
    mov1.operands = { ir::IRValue::imm(1) };
    mov1.address = 0x02000000;

    ir::IRInstruction b;
    b.op = ir::IROperation::BRANCH;
    b.operands = { ir::IRValue::imm(0x02000010) };
    b.address = 0x02000004;
    b.is_terminator = true;

    blk1.instructions.push_back(mov1);
    blk1.instructions.push_back(b);
    blk1.successors = { 0x02000010 };

    ir::IRBasicBlock blk2;
    blk2.start = 0x02000010;
    blk2.end = 0x02000018;

    ir::IRInstruction mov2;
    mov2.op = ir::IROperation::MOV;
    mov2.dest = ir::IRValue::reg(0);
    mov2.operands = { ir::IRValue::imm(2) };
    mov2.address = 0x02000010;

    ir::IRInstruction bxlr;
    bxlr.op = ir::IROperation::RETURN;
    bxlr.operands = { ir::IRValue::reg(14) };
    bxlr.address = 0x02000014;
    bxlr.is_terminator = true;

    blk2.instructions.push_back(mov2);
    blk2.instructions.push_back(bxlr);
    blk2.successors = {};

    fn.blocks = { 0x02000000, 0x02000010 };
    fn.block_map[0x02000000] = blk1;
    fn.block_map[0x02000010] = blk2;
    prog.functions[0x02000000] = fn;

    runtime::CPUState cpu{};
    cpu.r[14] = 0x02000200;

    ir::IRInterpreter interp;
    interp.init(cpu);
    interp.set_program(prog);
    auto stats = interp.execute(fn, 100);

    ASSERT_TRUE(stats.stopped);
    ASSERT_EQ(cpu.r[0], 2u);
    ASSERT_EQ(cpu.r[15], 0x02000200u);
}

TEST_CASE(CPUState, ThumbCpsrSync) {
    runtime::CPUState cpu{};
    cpu.cpsr = 0x13;
    cpu.set_thumb(true);
    ASSERT_TRUE(cpu.thumb);
    ASSERT_TRUE((cpu.cpsr >> 5) & 1);
    cpu.set_thumb(false);
    ASSERT_FALSE(cpu.thumb);
    ASSERT_FALSE((cpu.cpsr >> 5) & 1);
    cpu.cpsr |= (1u<<5);
    cpu.sync_thumb_from_cpsr();
    ASSERT_TRUE(cpu.thumb);
}

TEST_CASE(ThumbDecoder, DecodeThumbMov) {
    // Thumb MOV R0, #1 = 0x2001
    uint8_t bytes[] = {0x01, 0x20};
    auto insts = arm::ThumbDecoder::decode_buffer(bytes, 0x02000000);
    ASSERT_TRUE(!insts.empty());
    ASSERT_EQ(insts[0].type, arm::InstructionType::MOV);
    ASSERT_TRUE(insts[0].is_thumb);
    ASSERT_EQ(insts[0].rd, 0);
}

TEST_CASE(Interpreter, ThumbSimpleExecution) {
    // Thumb: MOVS R0,#5 ; BX LR  (thumb)
    ir::IRProgram prog;
    ir::IRFunction fn;
    fn.address = 0x02000000;
    fn.thumb = true;
    ir::IRBasicBlock blk;
    blk.start = 0x02000000;
    blk.end = 0x02000004;
    blk.is_thumb = true;
    ir::IRInstruction mov;
    mov.op = ir::IROperation::MOV;
    mov.dest = ir::IRValue::reg(0);
    mov.operands = { ir::IRValue::imm(5) };
    mov.address = 0x02000000;
    mov.is_thumb = true;
    mov.size = 2;
    ir::IRInstruction ret;
    ret.op = ir::IROperation::RETURN;
    ret.operands = { ir::IRValue::reg(14) };
    ret.address = 0x02000002;
    ret.is_thumb = true;
    ret.size = 2;
    ret.is_terminator = true;
    blk.instructions.push_back(mov);
    blk.instructions.push_back(ret);
    fn.blocks.push_back(0x02000000);
    fn.block_map[0x02000000] = blk;
    prog.functions[0x02000000] = fn;
    runtime::CPUState cpu{};
    cpu.set_thumb(true);
    cpu.r[14] = 0x02000100;
    ir::IRInterpreter interp;
    interp.init(cpu);
    interp.set_program(prog);
    auto stats = interp.execute(fn, 100);
    ASSERT_TRUE(stats.stopped);
    ASSERT_EQ(cpu.r[0], 5u);
    ASSERT_EQ(stats.thumb_executed, 2u);
    ASSERT_EQ(stats.arm_executed, 0u);
}

TEST_CASE(Interpreter, ArmToThumbViaBx) {
    // ARM function calls BX to Thumb function via INDIRECT_BRANCH with bit0=1
    ir::IRProgram prog;
    // ARM caller
    ir::IRFunction arm_fn;
    arm_fn.address = 0x02000000;
    arm_fn.thumb = false;
    ir::IRBasicBlock arm_blk;
    arm_blk.start = 0x02000000;
    arm_blk.end = 0x02000008;
    ir::IRInstruction mov;
    mov.op = ir::IROperation::MOV;
    mov.dest = ir::IRValue::reg(0);
    mov.operands = { ir::IRValue::imm(0x02000101) }; // target Thumb addr with bit0=1
    mov.address = 0x02000000;
    ir::IRInstruction bx;
    bx.op = ir::IROperation::INDIRECT_BRANCH;
    bx.operands = { ir::IRValue::reg(0) };
    bx.address = 0x02000004;
    bx.is_terminator = true;
    bx.is_indirect = true;
    arm_blk.instructions.push_back(mov);
    arm_blk.instructions.push_back(bx);
    arm_fn.blocks.push_back(0x02000000);
    arm_fn.block_map[0x02000000] = arm_blk;
    prog.functions[0x02000000] = arm_fn;
    // Thumb callee not needed for this test; we just check that thumb flag is set and stops as indirect branch
    runtime::CPUState cpu{};
    cpu.set_thumb(false);
    ir::IRInterpreter interp;
    interp.init(cpu);
    interp.set_program(prog);
    auto stats = interp.execute(arm_fn, 100);
    ASSERT_TRUE(stats.stopped);
    ASSERT_EQ(stats.stop_reason, std::string("indirect branch"));
    ASSERT_TRUE(cpu.thumb);
    ASSERT_EQ(cpu.r[15], 0x02000100u);
}

TEST_CASE(Runtime, MemoryBusRegions) {
    uint8_t arm9[256]={};
    runtime::init_memory(arm9,256,0x02000000);
    runtime::clear_bus_stats();
    runtime::write32(0x02000000, 0x12345678);
    ASSERT_EQ(runtime::read32(0x02000000), 0x12345678u);
    // ITCM
    runtime::write32(0x00000000, 0xDEADBEEF);
    ASSERT_EQ(runtime::read32(0x00000000), 0xDEADBEEFu);
    // DTCM
    runtime::write32(0x027E0000, 0xCAFEBABE);
    ASSERT_EQ(runtime::read32(0x027E0000), 0xCAFEBABEu);
    // IO stub deterministic 0
    ASSERT_EQ(runtime::read32(0x04000000), 0u);
    runtime::write32(0x04000000, 0xFFFFFFFF);
    // BIOS read stub 0
    ASSERT_EQ(runtime::read32(0xFFFF0000), 0u);
    auto &bs = runtime::bus_stats();
    ASSERT_TRUE(bs.ram_reads > 0);
    ASSERT_TRUE(bs.itcm_writes > 0 || bs.itcm_reads > 0);
    ASSERT_TRUE(bs.dtcm_writes > 0);
    runtime::shutdown_memory();
}

TEST_CASE(Runtime, Cp15HandlesControl) {
    uint8_t dummy[4]={};
    runtime::init_memory(dummy,4,0x02000000);
    runtime::clear_bus_stats();
    // Read ID
    ASSERT_EQ(runtime::nds_mrc(15,0,0,0), 0x41059461u);
    // Write control and read back
    runtime::nds_mcr(15,0,0x00050078,1,0);
    ASSERT_EQ(runtime::nds_mrc(15,0,1,0), 0x00050078u);
    // ITCM/DTCM
    runtime::nds_mcr(15,0,0x00000001,9,1);
    ASSERT_TRUE(runtime::cp15_state().itcm_enable);
    runtime::shutdown_memory();
}

TEST_CASE(Runtime, SwiLayer) {
    uint8_t dummy[4]={};
    runtime::init_memory(dummy,4,0x02000000);
    runtime::clear_bus_stats();
    runtime::nds_swi(0x05); // VBlank
    runtime::nds_swi(0x0B); // CpuSet
    auto &bs = runtime::bus_stats();
    ASSERT_EQ(bs.swi_count, 2u);
    runtime::shutdown_memory();
}

// New test: BL → fallthrough → conditional instruction (TEQ) → MOV (T2/T3 integration)
TEST_CASE(Interpreter, BLFollowedByFallthroughComparision) {
    // Simulate: caller at 0x02000000 does MOV, BL 0x02000030, then TEQ, MOV; callee at 0x02000030 returns
    ir::IRProgram prog;
    // Callee function at 0x02000030
    ir::IRFunction callee;
    callee.address = 0x02000030;
    callee.thumb = false;
    ir::IRBasicBlock callee_blk;
    callee_blk.start = 0x02000030;
    callee_blk.end = 0x02000038;
    ir::IRInstruction c_mov;
    c_mov.op = ir::IROperation::MOV;
    c_mov.dest = ir::IRValue::reg(0);
    c_mov.operands = { ir::IRValue::imm(99) };
    c_mov.address = 0x02000030;
    ir::IRInstruction c_ret;
    c_ret.op = ir::IROperation::RETURN;
    c_ret.operands = { ir::IRValue::reg(14) };
    c_ret.address = 0x02000034;
    c_ret.is_terminator = true;
    callee_blk.instructions.push_back(c_mov);
    callee_blk.instructions.push_back(c_ret);
    callee.blocks.push_back(0x02000030);
    callee.block_map[0x02000030] = callee_blk;
    prog.functions[0x02000030] = callee;

    // Caller
    ir::IRFunction fn;
    fn.address = 0x02000000;
    fn.thumb = false;
    ir::IRBasicBlock entry_blk;
    entry_blk.start = 0x02000000;
    entry_blk.end = 0x02000008;
    ir::IRInstruction mov;
    mov.op = ir::IROperation::MOV;
    mov.dest = ir::IRValue::reg(0);
    mov.operands = { ir::IRValue::imm(0x1000) };
    mov.address = 0x02000000;
    ir::IRInstruction bl;
    bl.op = ir::IROperation::CALL;
    bl.operands = { ir::IRValue::imm(0x02000030) };
    bl.address = 0x02000004;
    bl.is_call = true;
    entry_blk.instructions.push_back(mov);
    entry_blk.instructions.push_back(bl);
    entry_blk.successors = { 0x02000008 };
    ir::IRBasicBlock after_ret;
    after_ret.start = 0x02000008;
    after_ret.end = 0x02000014;
    ir::IRInstruction teq;
    teq.op = ir::IROperation::TEQ;
    teq.operands = { ir::IRValue::reg(1), ir::IRValue::reg(0) };
    teq.address = 0x02000008;
    ir::IRInstruction mov2;
    mov2.op = ir::IROperation::MOV;
    mov2.dest = ir::IRValue::reg(2);
    mov2.operands = { ir::IRValue::imm(0x1234) };
    mov2.address = 0x0200000C;
    ir::IRInstruction ret;
    ret.op = ir::IROperation::RETURN;
    ret.operands = { ir::IRValue::reg(14) };
    ret.address = 0x02000010;
    ret.is_terminator = true;
    after_ret.instructions.push_back(teq);
    after_ret.instructions.push_back(mov2);
    after_ret.instructions.push_back(ret);
    after_ret.successors = {};
    fn.blocks = { 0x02000000, 0x02000008 };
    fn.block_map[0x02000000] = entry_blk;
    fn.block_map[0x02000008] = after_ret;
    prog.functions[0x02000000] = fn;

    runtime::CPUState cpu{};
    cpu.r[14] = 0x02000300;
    ir::IRInterpreter interp;
    interp.init(cpu);
    interp.set_program(prog);
    auto stats = interp.execute(fn, 100);
    ASSERT_TRUE(stats.instructions_executed >= 5u);
    ASSERT_EQ(stats.stop_reason, std::string("return"));
}

// === Dynamic Code Tests (J1) ===

TEST_CASE(DynamicCode, CopyToITCM) {
    // Setup memory with ITCM
    uint8_t itcm_buf[32*1024] = {};
    runtime::init_memory(itcm_buf, sizeof(itcm_buf), 0x00000000); // ITCM at 0x00000000

    // Write some simple ARM code to ITCM at 0x00000000
    // ARM code: MOV R0, #0x10; BX LR (opcodes: E3A00010 E12FFF1E)
    uint8_t code[8] = {0x10, 0x00, 0xA0, 0xE3, 0xE0, 0xFF, 0xFF, 0xE1};
    runtime::write32(0x00000000, *(uint32_t*)(code));
    runtime::write32(0x00000004, *(uint32_t*)(code + 4));

    // Track the write via DynamicCodeManager
    runtime::DynamicCodeManager::instance().note_write(0x00000000, 8, 0x02000000);

    // Verify region was created
    ASSERT_TRUE(runtime::DynamicCodeManager::instance().has_region(0x00000000));

    // Verify generation was bumped
    uint64_t gen_before = runtime::DynamicCodeManager::instance().current_generation();

    // Execute via ensure_function - should decode and register
    bool found = runtime::DynamicCodeManager::instance().ensure_function(0x00000000, false);
    ASSERT_TRUE(found);

    // Verify generation changed after subsequent write
    runtime::write32(0x00000000, 0x00000000); // write that invalidates
    uint64_t gen_after = runtime::DynamicCodeManager::instance().current_generation();
    ASSERT_GT(gen_after, gen_before);

    runtime::shutdown_memory();
}

TEST_CASE(DynamicCode, ExecuteDynamicARM) {
    // Setup minimal memory
    uint8_t itcm_buf[32*1024] = {};
    runtime::init_memory(itcm_buf, sizeof(itcm_buf), 0x00000000);

    // Write ARM code: MOV R0, #42; BX LR
    uint8_t code[8] = {0x42, 0x00, 0xA0, 0xE3, 0xE0, 0xFF, 0xFF, 0xE1};
    runtime::write32(0x00000000, *(uint32_t*)(code));
    runtime::write32(0x00000004, *(uint32_t*)(code + 4));

    // Track and decode
    runtime::DynamicCodeManager::instance().note_write(0x00000000, 8, 0x02000000);
    bool found = runtime::DynamicCodeManager::instance().ensure_function(0x00000000, false);
    ASSERT_TRUE(found);

    // Verify region has discovered functions/counts
    auto regions = runtime::DynamicCodeManager::instance().regions();
    ASSERT_GE(regions.size(), size_t(1));
    ASSERT_GT(regions[0].discovered_functions, 0u);

    runtime::shutdown_memory();
}

TEST_CASE(DynamicCode, InvalidateAfterWrite) {
    // Setup memory
    uint8_t itcm_buf[32*1024] = {};
    runtime::init_memory(itcm_buf, sizeof(itcm_buf), 0x00000000);

    // Write initial code
    uint8_t code1[8] = {0x01, 0x00, 0xA0, 0xE3, 0xE0, 0xFF, 0xFF, 0xE1}; // MOV R0, #1
    runtime::write32(0x00000000, *(uint32_t*)(code1));

    // Track and decode - first time
    runtime::DynamicCodeManager::instance().note_write(0x00000000, 8, 0x02000000);
    bool found1 = runtime::DynamicCodeManager::instance().ensure_function(0x00000000, false);
    ASSERT_TRUE(found1);

    auto regions = runtime::DynamicCodeManager::instance().regions();
    ASSERT_GT(regions[0].execution_count, 0u);
    uint64_t gen1 = regions[0].generation;

    // Write new code to ITCM (simulating DMA reload)
    uint8_t code2[8] = {0x02, 0x00, 0xA0, 0xE3, 0xE0, 0xFF, 0xFF, 0xE1}; // MOV R0, #2
    runtime::write32(0x00000000, *(uint32_t*)(code2));

    // Track the new write - generation should bump
    runtime::DynamicCodeManager::instance().note_write(0x00000000, 8, 0x02000000);

    // Verify generation changed
    uint64_t gen2 = runtime::DynamicCodeManager::instance().current_generation();
    ASSERT_GT(gen2, gen1);

    // Re-decode should find new code
    bool found2 = runtime::DynamicCodeManager::instance().ensure_function(0x00000000, false);
    ASSERT_TRUE(found2);

    regions = runtime::DynamicCodeManager::instance().regions();
    // execution_count should reset or be updated
    // The key point is generation changed

    runtime::shutdown_memory();
}

TEST_CASE(DynamicCode, DynamicCodeRegionsJSON) {
    // Verify DynamicCodeManager can produce region info for JSON output
    uint8_t itcm_buf[32*1024] = {};
    runtime::init_memory(itcm_buf, sizeof(itcm_buf), 0x00000000);

    // Write some code
    uint8_t code[8] = {0x01, 0x00, 0xA0, 0xE3, 0xE0, 0xFF, 0xFF, 0xE1};
    runtime::write32(0x00000000, *(uint32_t*)(code));

    // Track and decode
    runtime::DynamicCodeManager::instance().note_write(0x00000000, 8, 0x02000000);
    runtime::DynamicCodeManager::instance().ensure_function(0x00000000, false);

    // Generate JSON via regions()
    auto dyn_regions = runtime::DynamicCodeManager::instance().regions();
    ASSERT_TRUE(dyn_regions.size() > 0);
    ASSERT_TRUE(dyn_regions[0].size > 0);
    ASSERT_TRUE(dyn_regions[0].discovered_functions > 0);

    runtime::shutdown_memory();
}
