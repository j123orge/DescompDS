#include "test_framework.h"
#include "arm/arm_decoder.h"
#include "analysis/basic_block.h"
#include "analysis/function.h"
#include "analysis/cfg.h"
#include <iostream>

using namespace descomp::arm;
using namespace descomp::analysis;

TEST_CASE(CFGAnalysis, BasicBlockBoundaries) {
    // Construct instructions representing:
    // 0x02000000: MOV R0, #0
    // 0x02000004: CMP R0, #10
    // 0x02000008: BNE 0x02000014 (offset = (0x14 - 0x10)/4 = 1 -> 0x1A000001)
    // 0x0200000C: MOV R1, #1
    // 0x02000010: BX LR
    // 0x02000014: MOV R1, #2
    // 0x02000018: BX LR

    std::vector<ARMInstruction> insts = {
        ARMDecoder::decode(0xE3A00000, 0x02000000), // MOV R0, #0
        ARMDecoder::decode(0xE350000A, 0x02000004), // CMP R0, #10
        ARMDecoder::decode(0x1A000001, 0x02000008), // BNE 0x02000014
        ARMDecoder::decode(0xE3A01001, 0x0200000C), // MOV R1, #1
        ARMDecoder::decode(0xE12FFF1E, 0x02000010), // BX LR
        ARMDecoder::decode(0xE3A01002, 0x02000014), // MOV R1, #2
        ARMDecoder::decode(0xE12FFF1E, 0x02000018)  // BX LR
    };

    auto blocks = BasicBlockBuilder::build_blocks(insts, { 0x02000000 });

    ASSERT_EQ(blocks.size(), 3);

    // Block 1: 0x02000000 .. 0x0200000C (3 instructions)
    ASSERT_TRUE(blocks.find(0x02000000) != blocks.end());
    const auto& b1 = blocks[0x02000000];
    ASSERT_EQ(b1.instruction_count(), 3);
    ASSERT_EQ(b1.successors.size(), 2);
    // Successor 0: 0x02000014 (True), Successor 1: 0x0200000C (False)
    ASSERT_EQ(b1.successors[0], 0x02000014U);
    ASSERT_EQ(b1.successors[1], 0x0200000CU);

    // Block 2: 0x0200000C .. 0x02000014 (2 instructions, ends in BX LR)
    ASSERT_TRUE(blocks.find(0x0200000C) != blocks.end());
    const auto& b2 = blocks[0x0200000C];
    ASSERT_EQ(b2.instruction_count(), 2);
    ASSERT_TRUE(b2.is_exit);
    ASSERT_EQ(b2.successors.size(), 0);

    // Block 3: 0x02000014 .. 0x0200001C (2 instructions, ends in BX LR)
    ASSERT_TRUE(blocks.find(0x02000014) != blocks.end());
    const auto& b3 = blocks[0x02000014];
    ASSERT_EQ(b3.instruction_count(), 2);
    ASSERT_TRUE(b3.is_exit);
    ASSERT_EQ(b3.successors.size(), 0);
}

TEST_CASE(CFGAnalysis, FunctionDiscoveryAndCallGraph) {
    // Subroutine func2 at 0x02000050:
    // 0x02000050: MOV R0, #42
    // 0x02000054: BX LR

    // Function func1 at 0x02000000:
    // 0x02000000: PUSH {LR}
    // 0x02000004: BL 0x02000050 (offset = (0x50 - 0x0C)/4 = 0x44/4 = 17 = 0x11 -> 0xEB000011)
    // 0x02000008: POP {PC}

    std::vector<ARMInstruction> insts = {
        // func1
        ARMDecoder::decode(0xE92D4000, 0x02000000), // PUSH {LR}
        ARMDecoder::decode(0xEB000011, 0x02000004), // BL 0x02000050
        ARMDecoder::decode(0xE8BD8000, 0x02000008), // POP {PC}

        // func2
        ARMDecoder::decode(0xE3A0002A, 0x02000050), // MOV R0, #42
        ARMDecoder::decode(0xE12FFF1E, 0x02000054)  // BX LR
    };

    auto blocks = BasicBlockBuilder::build_blocks(insts, { 0x02000000 });
    auto functions = FunctionDiscoverer::discover_functions(blocks, { 0x02000000 });

    ASSERT_EQ(functions.size(), 2);

    // Validate func1 (0x02000000)
    ASSERT_TRUE(functions.find(0x02000000) != functions.end());
    const auto& fn1 = functions[0x02000000];
    ASSERT_EQ(fn1.callees.size(), 1);
    ASSERT_EQ(fn1.callees[0], 0x02000050U);

    // Validate func2 (0x02000050)
    ASSERT_TRUE(functions.find(0x02000050) != functions.end());
    const auto& fn2 = functions[0x02000050];
    ASSERT_EQ(fn2.callers.size(), 1);
    ASSERT_EQ(fn2.callers[0], 0x02000000U);
}

TEST_CASE(CFGAnalysis, DotGraphExport) {
    std::vector<ARMInstruction> insts = {
        ARMDecoder::decode(0xE3A00000, 0x02000000), // MOV R0, #0
        ARMDecoder::decode(0xE12FFF1E, 0x02000004)  // BX LR
    };

    auto blocks = BasicBlockBuilder::build_blocks(insts, { 0x02000000 });
    auto functions = FunctionDiscoverer::discover_functions(blocks, { 0x02000000 });

    ASSERT_TRUE(functions.find(0x02000000) != functions.end());
    CFG cfg(functions[0x02000000], blocks);

    std::string dot = cfg.export_to_dot();
    ASSERT_STR_CONTAINS(dot, "digraph \"func_02000000\"");
    ASSERT_STR_CONTAINS(dot, "block_02000000");
    ASSERT_STR_CONTAINS(dot, "MOV r0, #0");
    ASSERT_STR_CONTAINS(dot, "BX lr");
}
