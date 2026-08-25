#include "runtime/dynamic_code.h"
#include "runtime/nds_runtime.h"
#include "arm/arm_decoder.h"
#include "arm/thumb_decoder.h"
#include "analysis/basic_block.h"
#include "analysis/function.h"
#include "ir/ir_translator.h"
#include <cstring>
#include <algorithm>

namespace descomp::runtime {

DynamicCodeManager& DynamicCodeManager::instance() {
    static DynamicCodeManager inst;
    return inst;
}

void DynamicCodeManager::clear() {
    regions_.clear();
    region_map_.clear();
}

bool DynamicCodeManager::has_region(uint32_t dest) const {
    return region_map_.find(dest) != region_map_.end();
}

void DynamicCodeManager::note_write(uint32_t dest, uint32_t size, uint32_t source_hint) {
    // Only track ITCM/DTCM and Main RAM writes that could be code
    bool is_itcm = (dest < 0x00008000);
    bool is_dtcm = (dest >= 0x027E0000 && dest < 0x027E4000);
    bool is_main = (dest >= 0x02000000 && dest < 0x02400000);
    if (!is_itcm && !is_dtcm && !is_main) return;

    // Bump generation to invalidate old recompiled code
    generation_counter++;
    if (generation_counter == 0) generation_counter = 1;

    // Coalesce with existing region if overlapping/adjacent
    for (auto &r : regions_) {
        if (dest >= r.destination && dest < r.destination + r.size + 0x1000) {
            // Extend
            uint32_t end = std::max(r.destination + r.size, dest + size);
            r.size = end - r.destination;
            r.write_count++;
            region_map_[r.destination] = r;
            return;
        }
        if (r.destination >= dest && r.destination < dest + size + 0x1000) {
            uint32_t end = std::max(dest + size, r.destination + r.size);
            r.destination = std::min(dest, r.destination);
            r.size = end - r.destination;
            r.write_count++;
            region_map_[r.destination] = r;
            return;
        }
    }

    DynamicCodeRegion reg;
    reg.source = source_hint;
    reg.destination = dest;
    reg.size = size;
    reg.permissions = "rwx";
    reg.is_thumb = false;
    reg.write_count = 1;
    regions_.push_back(reg);
    region_map_[dest] = reg;
}

bool DynamicCodeManager::ensure_function(uint32_t addr, bool thumb) {
    // Check if we already have a region containing addr
    for (auto &r : regions_) {
        if (addr >= r.destination && addr < r.destination + r.size) {
            // If region generation changed since last decode, re-decode
            bool needs_redecode = (r.generation != generation_counter);
            if (needs_redecode || r.discovered_functions == 0) {
                bool result = decode_and_register(addr, thumb);
                if (result) r.execution_count++;
                return result;
            }
            r.execution_count++;
            return false;
        }
    }
    // No region yet, but PC is trying to execute there - create a new region on demand
    // This handles cases where code was copied but not tracked via note_write (e.g., via DMA)
    DynamicCodeRegion reg;
    reg.destination = addr & ~0xFFFu; // page align
    reg.size = 0x1000;
    reg.is_thumb = thumb;
    reg.execution_count = 1;
    reg.generation = generation_counter;
    regions_.push_back(reg);
    region_map_[reg.destination] = reg;
    return decode_and_register(addr, thumb);
}

bool DynamicCodeManager::decode_and_register(uint32_t dest, bool thumb) {
    // Decode ITCM/DTCM memory at dest as ARM code using ARMDecoder
    // Then build BasicBlocks, discover functions, lift to IR
    // Register the function in the IRProgram for the interpreter

    const uint8_t* base = nullptr;
    size_t base_size = 0;
    bool is_itcm = (dest < 0x00008000);

    if (is_itcm) {
        base = runtime::get_itcm_data();
        base_size = runtime::get_itcm_size();
    } else {
        base = runtime::get_dtcm_data();
        base_size = runtime::get_dtcm_size();
    }

    if (!base || dest >= base_size) return false;

    size_t off = dest;
    size_t avail = std::min<size_t>(1024, base_size - off);
    if (avail < 4) return false;

    std::vector<uint8_t> buf(base + off, base + off + avail);

    // Decode as ARM instructions
    auto insts = arm::ARMDecoder::decode_buffer(buf, dest);
    if (insts.empty()) return false;

    // Build basic blocks from decoded instructions
    std::vector<uint32_t> entry = {dest};
    auto bbs = analysis::BasicBlockBuilder::build_blocks(insts, entry);
    if (bbs.empty()) return false;

    // Discover functions from basic blocks
    auto funcs = analysis::FunctionDiscoverer::discover_functions(bbs, entry);
    auto fit = funcs.find(dest);
    if (fit == funcs.end()) return false;

    // Lift to IR program
    auto prog = ir::IRTranslator::lift_program(bbs, funcs);
    auto fit2 = prog.functions.find(dest);
    if (fit2 == prog.functions.end()) return false;

    // Register the function - need access to mutable program
    // This will be called from the interpreter which has program access
    // For now, store the function info in the region
    for (auto &r : regions_) {
        if (dest >= r.destination && dest < r.destination + r.size) {
            r.discovered_functions = fit2->second.blocks.size();
            r.discovered_blocks = fit2->second.blocks.size();
            r.is_thumb = false;
            r.generation = generation_counter;
            break;
        }
    }

    // Try to add to the current interpreter's program
    // The interpreter should have set_program() called before
    return true;
}

} // namespace descomp::runtime
