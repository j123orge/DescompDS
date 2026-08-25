#include "nds/nds_image.h"
#include "arm/arm_decoder.h"
#include "arm/thumb_decoder.h"
#include "analysis/basic_block.h"
#include "analysis/function.h"
#include "analysis/cfg.h"
#include "analysis/validator.h"
#include "analysis/code_data.h"
#include "ir/ir_translator.h"
#include "lifter/cpp_lifter.h"
#include "runtime/nds_runtime.h"
#include "runtime/dynamic_code.h"
#include "runtime/subsystem_access.h"
#include "ir/ir_interpreter.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <filesystem>
#include <chrono>
#include <iomanip>

using namespace descomp;

static void print_banner() {
    std::cout << "=========================================================\n";
    std::cout << "  DescompDS - Nintendo DS Static Recompiler (Phase 1)    \n";
    std::cout << "  ROM Parser, ARM9/Thumb Disassembler, CFG & Analysis   \n";
    std::cout << "=========================================================\n\n";
}

static void print_usage(const char* prog_name) {
    std::cout << "Usage: " << prog_name << " <game.nds> [options]\n\n";
    std::cout << "Options:\n";
    std::cout << "  -o, --output <dir>   Set output directory (default: ./output)\n";
    std::cout << "  -x, --extract        Extract ARM9/ARM7 binaries and overlays\n";
    std::cout << "  -d, --disassemble    Generate full linear ARM9 disassembly\n";
    std::cout << "  -c, --cfg            Build CFG and export Graphviz .dot files\n";
    std::cout << "  --emit-cpp           Generate recompiled C++ (arm9_recompiled.cpp)\n";
    std::cout << "  --run                Execute ARM9 via IR interpreter (up to 1M insns)\n";
    std::cout << "  --trace              Enable instruction trace during --run\n";
    std::cout << "  --trace-limit N      Max trace lines (default: 200)\n";
    std::cout << "  --max-insns N        Max instructions for --run (default: 1000000)\n";
    std::cout << "  -a, --all            Perform extraction, disassembly, and CFG (default)\n";
    std::cout << "  -v, --verbose        Enable verbose output\n";
    std::cout << "  -h, --help           Show this help message\n\n";
    std::cout << "Example:\n";
    std::cout << "  " << prog_name << " mario.nds --run --trace --trace-limit 200\n";
    std::cout << "  " << prog_name << " mario.nds --run --max-insns 5000000\n";
    std::cout << "  " << prog_name << " mario.nds --output ./output_mario --emit-cpp\n";
}

struct ProgramOptions {
    std::filesystem::path rom_path;
    std::filesystem::path output_dir{"output"};
    bool extract{false};
    bool disassemble{false};
    bool generate_cfg{false};
    bool emit_cpp{false};
    bool run{false};
    bool trace{false};
    uint64_t trace_limit{200};
    uint64_t max_instructions{1000000};
    bool verbose{false};
};

static bool parse_cli_arguments(int argc, char* argv[], ProgramOptions& opt) {
    bool explicit_action = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            exit(0);
        } else if (arg == "-o" || arg == "--output") {
            if (i + 1 < argc) {
                opt.output_dir = argv[++i];
            } else {
                std::cerr << "Error: Missing argument for " << arg << "\n";
                return false;
            }
        } else if (arg == "-x" || arg == "--extract") {
            opt.extract = true;
            explicit_action = true;
        } else if (arg == "-d" || arg == "--disassemble") {
            opt.disassemble = true;
            explicit_action = true;
        } else if (arg == "-c" || arg == "--cfg") {
            opt.generate_cfg = true;
            explicit_action = true;
        } else if (arg == "-a" || arg == "--all") {
            opt.extract = true;
            opt.disassemble = true;
            opt.generate_cfg = true;
            explicit_action = true;
        } else if (arg == "--emit-cpp" || arg == "--emit_cpp") {
            opt.emit_cpp = true;
            explicit_action = true;
        } else if (arg == "--run") {
            opt.run = true;
            explicit_action = true;
        } else if (arg == "--trace") {
            opt.trace = true;
        } else if (arg == "--trace-limit") {
            if (i + 1 < argc) {
                opt.trace_limit = std::stoull(argv[++i]);
            } else {
                std::cerr << "Error: Missing argument for " << arg << "\n";
                return false;
            }
        } else if (arg == "--max-insns" || arg == "--max-instructions") {
            if (i + 1 < argc) {
                opt.max_instructions = std::stoull(argv[++i]);
            } else {
                std::cerr << "Error: Missing argument for " << arg << "\n";
                return false;
            }
        } else if (arg == "-v" || arg == "--verbose") {
            opt.verbose = true;
        } else if (arg.starts_with("-")) {
            std::cerr << "Error: Unknown option: " << arg << "\n";
            return false;
        } else {
            opt.rom_path = arg;
        }
    }

    if (opt.rom_path.empty()) {
        std::cerr << "Error: No .nds input file specified.\n\n";
        print_usage(argv[0]);
        return false;
    }

    // Default to running all steps if none explicitly requested
    if (!explicit_action) {
        opt.extract = true;
        opt.disassemble = true;
        opt.generate_cfg = true;
    }

    return true;
}

int main(int argc, char* argv[]) {
    print_banner();

    ProgramOptions opt;
    if (!parse_cli_arguments(argc, argv, opt)) {
        return 1;
    }

    auto start_time = std::chrono::high_resolution_clock::now();

    std::cout << "[+] Loading ROM: " << opt.rom_path.string() << "\n";
    std::string err_msg;
    auto rom = nds::NDSImage::load_from_file(opt.rom_path, &err_msg);
    if (!rom) {
        std::cerr << "[-] Error loading ROM: " << err_msg << "\n";
        return 1;
    }

    const auto& header = rom->header();
    std::cout << header.summary() << "\n";

    std::error_code ec;
    std::filesystem::create_directories(opt.output_dir, ec);
    if (ec) {
        std::cerr << "[-] Failed to create output directory: " << opt.output_dir.string() << "\n";
        return 1;
    }

    // 1. Extract binaries & rom_info.json
    if (opt.extract) {
        std::cout << "[+] Extracting ROM components to: " << opt.output_dir.string() << " ...\n";
        std::string extract_log;
        if (rom->extract_to(opt.output_dir, &extract_log)) {
            std::cout << "    [OK] " << extract_log << "\n";
        } else {
            std::cerr << "    [FAIL] " << extract_log << "\n";
        }
    }

    // 2. Decode ARM9 instructions
    auto arm9_bin = rom->arm9_binary();
    if (arm9_bin.empty()) {
        std::cerr << "[-] ARM9 binary section is empty or invalid.\n";
        return 1;
    }

    std::cout << "[+] Disassembling ARM9 executable (" << arm9_bin.size() << " bytes at RAM " 
              << std::hex << std::showbase << header.arm9_ram_address << ") ...\n" << std::dec << std::noshowbase;

    auto instructions = arm::ARMDecoder::decode_buffer(arm9_bin, header.arm9_ram_address);
    // Also decode as Thumb for pipeline support and validation (T2) - report only, do not merge into ARM flow
    // Thumb execution is validated via IR tests (ThumbSimpleExecution, ArmToThumbViaBx) and via separate overlay decodes
    {
        auto thumb_instructions = arm::ThumbDecoder::decode_buffer(arm9_bin, header.arm9_ram_address);
        size_t thumb_valid = 0;
        for (auto &ti : thumb_instructions) if (ti.type != arm::InstructionType::UNKNOWN) ++thumb_valid;
        std::cout << "    [OK] Decoded " << instructions.size() << " ARM instructions (" << thumb_valid << " valid Thumb in same buffer, not merged for ARM9 flow).\n";
    }

    // 3. Write linear disassembly report
    if (opt.disassemble) {
        std::filesystem::path disasm_path = opt.output_dir / "arm9_disassembly.txt";
        std::cout << "[+] Writing disassembly to: " << disasm_path.string() << " ...\n";
        std::ofstream disasm_file(disasm_path);
        if (disasm_file.is_open()) {
            disasm_file << "; =========================================================\n";
            disasm_file << "; DescompDS ARM9 Disassembly\n";
            disasm_file << "; Game: " << header.game_title << " [" << header.game_code << "]\n";
            disasm_file << "; Entrypoint: 0x" << std::hex << header.arm9_entry_address << "\n";
            disasm_file << "; =========================================================\n\n";

            for (const auto& inst : instructions) {
                if (inst.address == header.arm9_entry_address) {
                    disasm_file << "\n; --- ARM9 Entry Point ---\n";
                }
                disasm_file << inst.to_disasm_line() << "\n";
            }
            std::cout << "    [OK] Disassembly complete (" << instructions.size() << " lines).\n";
        } else {
            std::cerr << "    [FAIL] Failed to open " << disasm_path.string() << " for writing.\n";
        }
    }

    // 4. Basic Block Building & Function Discovery
    std::cout << "[+] Analyzing control flow and discovering basic blocks ...\n";
    std::vector<uint32_t> entry_points = { header.arm9_entry_address };
    for (const auto& ov : rom->arm9_overlays()) {
        if (ov.static_init_start != 0) {
            entry_points.push_back(ov.static_init_start);
        }
    }

    auto basic_blocks = analysis::BasicBlockBuilder::build_blocks(instructions, entry_points);
    std::cout << "    [OK] Identified " << basic_blocks.size() << " Basic Blocks.\n";

    std::cout << "[+] Discovering functions from entry points and call sites ...\n";
    auto functions = analysis::FunctionDiscoverer::discover_functions(basic_blocks, entry_points);
    std::cout << "    [OK] Discovered " << functions.size() << " functions.\n";

    // Write functions.json
    {
        std::filesystem::path func_json_path = opt.output_dir / "functions.json";
        std::ofstream func_file(func_json_path);
        if (func_file.is_open()) {
            func_file << "[\n";
            size_t idx = 0;
            for (const auto& [addr, fn] : functions) {
                func_file << "  " << fn.to_json(4) << (idx + 1 < functions.size() ? "," : "") << "\n";
                ++idx;
            }
            func_file << "]\n";
            std::cout << "    [OK] Saved function metadata to: " << func_json_path.string() << "\n";
        }
    }

    // 4b. Generate callgraph.json (inter-procedural call edges from Function::callees/callers)
    {
        std::filesystem::path cg_path = opt.output_dir / "callgraph.json";
        std::ofstream jf(cg_path);
        if (jf.is_open()) {
            // Build a set of executed function addresses from deep_trace (if available)
            std::set<uint32_t> executed_funcs;
            // We'll populate this later when --run is available; for now use all functions
            // as "potentially executable"

            // Collect all unique function addresses for nodes
            std::set<uint32_t> all_func_addrs;
            for (const auto& [addr, fn] : functions) {
                all_func_addrs.insert(addr);
                for (uint32_t c : fn.callees) all_func_addrs.insert(c);
                for (uint32_t r : fn.callers) all_func_addrs.insert(r);
            }

            jf << "{\n";
            jf << "  \"nodes\": [\n";
            size_t nidx = 0;
            for (uint32_t addr : all_func_addrs) {
                auto it = functions.find(addr);
                if (it != functions.end()) {
                    const auto& fn = it->second;
                    jf << "    {\"address\":\"0x" << std::hex << std::setw(8) << std::setfill('0') << addr
                       << std::dec << "\",\"name\":\"" << fn.name
                       << "\",\"blocks\":" << fn.blocks.size()
                       << ",\"size\":" << fn.size
                       << ",\"thumb\":" << (fn.thumb ? "true" : "false")
                       << ",\"callees\":" << fn.callees.size()
                       << ",\"callers\":" << fn.callers.size()
                       << ",\"executed\":false"
                       << ",\"dynamic\":false"
                       << "}";
                } else {
                    // Function address known from callees/callers but not in functions map (external)
                    jf << "    {\"address\":\"0x" << std::hex << std::setw(8) << std::setfill('0') << addr
                       << std::dec << "\",\"name\":\"unknown\",\"blocks\":0,\"size\":0"
                       << ",\"thumb\":false,\"callees\":0,\"callers\":0"
                       << ",\"executed\":false,\"dynamic\":false}";
                }
                if (++nidx < all_func_addrs.size()) jf << ",";
                jf << "\n";
            }
            jf << "  ],\n";

            // Build edges from callees
            jf << "  \"edges\": [\n";
            std::set<std::pair<uint32_t,uint32_t>> seen_edges;
            size_t eidx = 0;
            size_t total_edges = 0;
            for (const auto& [addr, fn] : functions) {
                for (uint32_t callee : fn.callees) {
                    auto edge = std::make_pair(addr, callee);
                    if (seen_edges.find(edge) == seen_edges.end()) {
                        seen_edges.insert(edge);
                        total_edges++;
                    }
                }
            }
            for (const auto& [addr, fn] : functions) {
                for (uint32_t callee : fn.callees) {
                    auto edge = std::make_pair(addr, callee);
                    if (seen_edges.count(edge)) {
                        seen_edges.erase(edge); // only emit once
                        jf << "    {\"from\":\"0x" << std::hex << std::setw(8) << std::setfill('0') << addr
                           << "\",\"to\":\"0x" << std::setw(8) << callee
                           << std::dec << "\",\"type\":\"BL\",\"executed\":false,\"count\":0}";
                        if (++eidx < total_edges) jf << ",";
                        jf << "\n";
                    }
                }
            }
            jf << "  ]\n";
            jf << "}\n";
            std::cout << "    [OK] Wrote " << cg_path.string() << " (" << all_func_addrs.size()
                      << " nodes, " << total_edges << " edges)\n";
        }
    }

    // 5. CFG Generation and DOT Export
    if (opt.generate_cfg) {
        std::filesystem::path cfg_dir = opt.output_dir / "cfg";
        std::cout << "[+] Generating CFG graphs (.dot) to: " << cfg_dir.string() << " ...\n";
        std::string cfg_err;
        if (analysis::CFG::export_all(cfg_dir, functions, basic_blocks, &cfg_err)) {
            std::cout << "    [OK] Exported " << functions.size() << " CFG .dot files.\n";
        } else {
            std::cerr << "    [FAIL] " << cfg_err << "\n";
        }
    }

    // 6. Phase 1.5 Validation Report
    std::cout << "[+] Running Phase 1.5 Analysis Validation (ARM9 + 103 Overlays) ...\n";
    auto val_report = analysis::Phase1Validator::validate_rom(*rom);
    std::filesystem::path val_path = opt.output_dir / "phase1_validation.json";
    if (val_report.save_to_file(val_path)) {
        std::cout << "    [OK] Saved validation report to: " << val_path.string() << "\n";
        std::cout << "\n" << val_report.summary() << "\n";
    } else {
        std::cerr << "    [FAIL] Failed to write " << val_path.string() << "\n";
    }

    // 7. Emit C++ (Fase 2.5/2.6)
    if (opt.emit_cpp) {
        std::cout << "[+] Generating recompiled C++ (--emit-cpp) ...\n";
        auto prog = ir::IRTranslator::lift_program(basic_blocks, functions);
        auto code_data_map = analysis::CodeDataClassifier::classify(instructions, basic_blocks, functions, header.arm9_ram_address, arm9_bin.size());
        auto code_stats = analysis::CodeDataClassifier::analyze(instructions, basic_blocks, functions, header.arm9_ram_address, arm9_bin.size());
        auto lifter_stats = lifter::CppLifter::analyze(prog);

        std::filesystem::path gen_dir = opt.output_dir / "generated";
        std::filesystem::create_directories(gen_dir, ec);
        std::filesystem::path func_dir = gen_dir / "functions";
        std::filesystem::create_directories(func_dir, ec);

        std::string cpp = lifter::CppLifter::lift_program(prog);
        size_t unimpl_calls = 0;
        {
            size_t pos = 0;
            while ((pos = cpp.find("nds_unimplemented(0x", pos)) != std::string::npos) { ++unimpl_calls; ++pos; }
        }

        {
            std::ofstream out(gen_dir / "arm9_recompiled.cpp");
            if (out.is_open()) {
                out << cpp;
                std::cout << "    [OK] Wrote " << (gen_dir / "arm9_recompiled.cpp").string() << " (" << cpp.size() << " bytes)\n";
            } else {
                std::cerr << "    [FAIL] Could not write arm9_recompiled.cpp\n";
            }
        }
        {
            std::ofstream hdr(gen_dir / "generated.h");
            if (hdr.is_open()) {
                hdr << "#pragma once\n#include <cstdint>\n";
                for (auto & [addr, fn] : prog.functions) {
                    hdr << "void func_" << std::hex << std::setw(8) << std::setfill('0') << addr << std::uppercase << "(descomp::runtime::CPUState&);\n";
                }
            }
        }
        {
            std::ofstream rt(gen_dir / "runtime_stubs.h");
            if (rt.is_open()) {
                rt << "#pragma once\n#include <cstdint>\n";
                rt << "static inline uint32_t nds_hal_read32(uint32_t a){(void)a;return 0;}\n";
                rt << "static inline uint16_t nds_hal_read16(uint32_t a){(void)a;return 0;}\n";
                rt << "static inline uint8_t nds_hal_read8(uint32_t a){(void)a;return 0;}\n";
                rt << "static inline void nds_hal_write32(uint32_t a,uint32_t v){(void)a;(void)v;}\n";
                rt << "static inline void nds_hal_write16(uint32_t a,uint16_t v){(void)a;(void)v;}\n";
                rt << "static inline void nds_hal_write8(uint32_t a,uint8_t v){(void)a;(void)v;}\n";
                rt << "static inline void nds_unimplemented(uint32_t a,uint32_t r){(void)a;(void)r;}\n";
                rt << "static inline void nds_swi(uint32_t id){(void)id;}\n";
                rt << "static inline void nds_bkpt(uint32_t id){(void)id;}\n";
            }
        }
        // Per-function files (optional, for inspection)
        for (auto & [addr, fn] : prog.functions) {
            std::ostringstream hs; hs << "func_" << std::hex << std::setw(8) << std::setfill('0') << std::uppercase << addr << ".cpp";
            std::ofstream f(func_dir / hs.str());
            if (f.is_open()) {
                f << lifter::CppLifter::lift_function(fn);
            }
        }

        std::cout << "    [STATS] Functions: " << prog.function_count() << "  Blocks: " << prog.block_count() << "\n";
        std::cout << "    [STATS] ARM/Thumb decoded: " << instructions.size() << "  CODE: " << code_stats.code_instructions << "  DATA: " << code_stats.data_words << "  UNKNOWN: " << code_stats.unknown_words << "\n";
        std::cout << "    [STATS] IR total: " << lifter_stats.total_instructions << "  Converted: " << lifter_stats.converted << "  UNKNOWN IR: " << lifter_stats.unknown << " (" << (lifter_stats.total_instructions? (100.0*lifter_stats.converted/lifter_stats.total_instructions):0) << "%)\n";
        std::cout << "    [STATS] nds_unimplemented calls in C++: " << unimpl_calls << "\n";
    }

    // 8. Run ARM9 (IR interpreter mode)
    if (opt.run) {
        std::cout << "[+] Executing ARM9 with IR interpreter (--run) ...\n";
        auto prog = ir::IRTranslator::lift_program(basic_blocks, functions);

        // Find the entry function
        auto entry_it = prog.functions.find(header.arm9_entry_address);
        if (entry_it == prog.functions.end()) {
            std::cerr << "    [FAIL] No recompiled function found at ARM9 entry point 0x"
                      << std::hex << header.arm9_entry_address << "\n";
            return 1;
        }

        // Init memory
        runtime::init_memory(arm9_bin.data(), arm9_bin.size(), header.arm9_ram_address);
        runtime::clear_bus_stats();

        // Init CPU
        runtime::CPUState cpu{};
        cpu.r[15] = header.arm9_entry_address + 8; // ARM pipeline: PC = current+8
        cpu.r[13] = 0x023FFF00; // Typical NDS ARM9 stack (will be overwritten by code)
        cpu.cpsr = 0x1D; // System mode
        cpu.thumb = false;

        ir::IRInterpreter interp;
        interp.init(cpu);
        interp.set_program(prog);
        interp.trace_enabled = opt.trace;
        interp.trace_limit = opt.trace_limit;
        interp.deep_trace_enabled = true;
        interp.deep_trace_limit = 5000;
        interp.clear_deep_trace();

        uint64_t max_insns = 1000000;
        std::cout << "    Entrypoint: 0x" << std::hex << header.arm9_entry_address
                  << "  Max instructions: " << std::dec << max_insns << "\n";

        auto run_start = std::chrono::high_resolution_clock::now();
        auto stats = interp.execute(entry_it->second, max_insns);
        auto run_end = std::chrono::high_resolution_clock::now();
        auto run_ms = std::chrono::duration_cast<std::chrono::milliseconds>(run_end - run_start).count();

        std::cout << "\n    ===== Execution Result =====\n";
        std::cout << "    Instructions executed: " << stats.instructions_executed << "\n";
        std::cout << "    ARM executed: " << stats.arm_executed << "  Thumb executed: " << stats.thumb_executed << "\n";
        std::cout << "    Functions executed: " << stats.functions_executed << "  Blocks executed: " << stats.blocks_executed << "\n";
        std::cout << "    BL calls: " << stats.bl_calls << "  BX returns: " << stats.bx_returns << "\n";
        auto &bs = runtime::bus_stats();
        std::cout << "    RAM reads=" << bs.ram_reads << " writes=" << bs.ram_writes
                  << "  ITCM r=" << bs.itcm_reads << " w=" << bs.itcm_writes
                  << "  DTCM r=" << bs.dtcm_reads << " w=" << bs.dtcm_writes << "\n";
        std::cout << "    IO reads=" << bs.io_reads << " writes=" << bs.io_writes
                  << "  BIOS reads=" << bs.bios_reads << "  ROM reads=" << bs.rom_reads
                  << "  Unmapped r=" << bs.unmapped_reads << " w=" << bs.unmapped_writes << "\n";
        std::cout << "    CP15 reads=" << bs.cp15_reads << " writes=" << bs.cp15_writes << " unknown=" << bs.unknown_cp15 << "\n";
        std::cout << "    SWI count=" << bs.swi_count << " unknown=" << bs.unknown_swi << "\n";
        std::cout << "    Unknown IR encountered: " << stats.unknown_encountered << "\n";
        std::cout << "    Execution time: " << run_ms << " ms\n";
        std::cout << "    Stopped: " << (stats.stopped ? "yes" : "no") << "\n";
        if (stats.stopped) {
            std::cout << "    Stop reason: " << stats.stop_reason << "\n";
            std::cout << "    Stop address: 0x" << std::hex << stats.stop_address << std::dec << "\n";
        }
        std::cout << "\n    CPU State at stop:\n";
        for (int i = 0; i < 16; ++i) {
            std::cout << "    R" << std::dec << i << " = 0x" << std::hex << std::setw(8)
                      << std::setfill('0') << cpu.r[i] << "\n";
        }
        std::cout << "    CPSR = 0x" << std::hex << std::setw(8) << std::setfill('0') << cpu.cpsr
                  << "  N=" << (int)cpu.flag_n() << " Z=" << (int)cpu.flag_z()
                  << " C=" << (int)cpu.flag_c() << " V=" << (int)cpu.flag_v()
                  << "  Thumb=" << (cpu.thumb ? "yes" : "no") << std::dec << "\n";

        // Generate run_trace_summary.json
        {
            std::filesystem::path summary_path = opt.output_dir / "run_trace_summary.json";
            std::ofstream jf(summary_path);
            if (jf.is_open()) {
                jf << "{\n";
                jf << "  \"total_instructions\": " << stats.instructions_executed << ",\n";
                jf << "  \"arm_executed\": " << stats.arm_executed << ",\n";
                jf << "  \"thumb_executed\": " << stats.thumb_executed << ",\n";
                jf << "  \"functions_executed\": " << stats.functions_executed << ",\n";
                jf << "  \"blocks_executed\": " << stats.blocks_executed << ",\n";
                jf << "  \"bl_calls\": " << stats.bl_calls << ",\n";
                jf << "  \"bx_returns\": " << stats.bx_returns << ",\n";
                jf << "  \"ram_reads\": " << bs.ram_reads << ",\n";
                jf << "  \"ram_writes\": " << bs.ram_writes << ",\n";
                jf << "  \"itcm_reads\": " << bs.itcm_reads << ",\n";
                jf << "  \"itcm_writes\": " << bs.itcm_writes << ",\n";
                jf << "  \"dtcm_reads\": " << bs.dtcm_reads << ",\n";
                jf << "  \"dtcm_writes\": " << bs.dtcm_writes << ",\n";
                jf << "  \"io_reads\": " << bs.io_reads << ",\n";
                jf << "  \"io_writes\": " << bs.io_writes << ",\n";
                jf << "  \"bios_reads\": " << bs.bios_reads << ",\n";
                jf << "  \"rom_reads\": " << bs.rom_reads << ",\n";
                jf << "  \"unmapped_reads\": " << bs.unmapped_reads << ",\n";
                jf << "  \"unmapped_writes\": " << bs.unmapped_writes << ",\n";
                jf << "  \"cp15_reads\": " << bs.cp15_reads << ",\n";
                jf << "  \"cp15_writes\": " << bs.cp15_writes << ",\n";
                jf << "  \"cp15_unknown\": " << bs.unknown_cp15 << ",\n";
                jf << "  \"swi_count\": " << bs.swi_count << ",\n";
                jf << "  \"swi_unknown\": " << bs.unknown_swi << ",\n";
                jf << "  \"unknown_ir\": " << stats.unknown_encountered << ",\n";
                jf << "  \"stop_address\": \"0x" << std::hex << std::setw(8) << std::setfill('0') << stats.stop_address << "\",\n" << std::dec;
                jf << "  \"stop_reason\": \"" << stats.stop_reason << "\",\n";
                jf << "  \"cpsr\": \"0x" << std::hex << std::setw(8) << std::setfill('0') << cpu.cpsr << "\",\n" << std::dec;
                jf << "  \"thumb\": " << (cpu.thumb ? "true" : "false") << ",\n";
                jf << "  \"r\": [";
                for (int i=0;i<16;++i){ if(i) jf<<","; jf << "\"0x" << std::hex << std::setw(8) << std::setfill('0') << cpu.r[i] << "\""; }
                jf << std::dec << "]\n";
                jf << "}\n";
                std::cout << "    [OK] Wrote " << summary_path.string() << "\n";
            }
        }

        // Generate deep_trace.json (last 5000)
        {
            std::filesystem::path deep_path = opt.output_dir / "deep_trace.json";
            std::ofstream jf(deep_path);
            if (jf.is_open()) {
                auto &dt = interp.get_deep_trace();
                jf << "[\n";
                for (size_t i=0;i<dt.size();++i) {
                    auto &e = dt[i];
                    jf << "  {\"idx\":" << i << ",\"pc\":\"0x" << std::hex << std::setw(8) << std::setfill('0') << e.pc << std::dec
                       << "\",\"raw\":\"0x" << std::hex << std::setw(8) << e.raw << std::dec
                       << "\",\"thumb\":" << (e.thumb?"true":"false")
                       << ",\"func\":\"0x" << std::hex << std::setw(8) << e.func << std::dec << "\""
                       << ",\"block\":\"0x" << std::hex << std::setw(8) << e.block << std::dec << "\""
                       << ",\"mnemonic\":\"" << e.mnemonic << "\""
                       << ",\"cpsr\":\"0x" << std::hex << std::setw(8) << e.cpsr << std::dec << "\""
                       << ",\"is_branch\":" << (e.is_branch?"true":"false")
                       << ",\"branch_taken\":" << (e.branch_taken?"true":"false");
                    if (!e.cond_str.empty()) jf << ",\"cond\":\"" << e.cond_str << "\"";
                    jf << ",\"r\":[";
                    for(int r=0;r<16;++r){ if(r) jf<<","; jf<<"\"0x"<<std::hex<<std::setw(8)<<std::setfill('0')<<e.r[r]<<"\""; }
                    jf << std::dec << "]";
                    if (e.has_mem_read) jf << ",\"mem_read\":{\"addr\":\"0x"<<std::hex<<std::setw(8)<<e.mem_read_addr<<"\",\"val\":\"0x"<<std::setw(8)<<e.mem_read_val<<"\",\"size\":"<<std::dec<<e.mem_read_size<<"}";
                    if (e.has_mem_write) jf << ",\"mem_write\":{\"addr\":\"0x"<<std::hex<<std::setw(8)<<e.mem_write_addr<<"\",\"val\":\"0x"<<std::setw(8)<<e.mem_write_val<<"\",\"size\":"<<std::dec<<e.mem_write_size<<"}";
                    jf << "}";
                    if (i+1<dt.size()) jf << ",";
                    jf << "\n";
                }
                jf << "]\n";
                std::cout << "    [OK] Wrote " << deep_path.string() << " (" << dt.size() << " entries)\n";
            }
        }

        // Generate loop_analysis.json
        {
            std::filesystem::path loop_path = opt.output_dir / "loop_analysis.json";
            std::ofstream jf(loop_path);
            if (jf.is_open()) {
                auto &dt = interp.get_deep_trace();
                // Count PC frequencies
                std::map<uint32_t, size_t> pc_count;
                std::map<uint32_t, std::string> pc_mn;
                for (auto &e: dt) { pc_count[e.pc]++; pc_mn[e.pc]=e.mnemonic; }
                // Find most frequent PC
                uint32_t hot_pc = 0; size_t hot_cnt=0;
                for (auto &kv: pc_count) if(kv.second>hot_cnt){hot_cnt=kv.second; hot_pc=kv.first;}
                // Define loop as PCs with high frequency (> dt.size()*0.05)
                size_t threshold = dt.size()/20; // 5%
                std::vector<uint32_t> loop_pcs;
                for (auto &kv: pc_count) if(kv.second>=threshold) loop_pcs.push_back(kv.first);
                std::sort(loop_pcs.begin(), loop_pcs.end());
                uint32_t loop_start = loop_pcs.empty()? hot_pc : loop_pcs.front();
                uint32_t loop_end = loop_pcs.empty()? hot_pc : loop_pcs.back();
                // Find branch responsible: most frequent is_branch with taken
                std::map<uint32_t, size_t> branch_pc;
                for (auto &e: dt) if(e.is_branch && e.branch_taken) branch_pc[e.pc]++;
                uint32_t branch_pc_hot=0; size_t bhot=0;
                for(auto &kv: branch_pc) if(kv.second>bhot){bhot=kv.second; branch_pc_hot=kv.first;}
                // Registers modified: track which regs change across loop
                std::set<int> modified_regs;
                std::set<int> invariant_regs;
                // For simplicity, check first vs last entry in loop
                if (!dt.empty()) {
                    auto first = dt.front();
                    auto last = dt.back();
                    for(int r=0;r<16;++r) if(first.r[r]!=last.r[r]) modified_regs.insert(r); else invariant_regs.insert(r);
                }
                // Memory accessed in loop
                std::set<uint32_t> mem_addrs;
                for(auto &e: dt) {
                    if(e.has_mem_read) mem_addrs.insert(e.mem_read_addr & ~0xFFFu); // page
                    if(e.has_mem_write) mem_addrs.insert(e.mem_write_addr & ~0xFFFu);
                }
                jf << "{\n";
                jf << "  \"hot_pc\":\"0x" << std::hex << std::setw(8) << std::setfill('0') << hot_pc << std::dec << "\",\"hot_count\":" << hot_cnt << ",\n";
                jf << "  \"loop_start\":\"0x" << std::hex << std::setw(8) << loop_start << std::dec << "\",\n";
                jf << "  \"loop_end\":\"0x" << std::hex << std::setw(8) << loop_end << std::dec << "\",\n";
                jf << "  \"loop_pcs\":[";
                for(size_t i=0;i<loop_pcs.size();++i){ if(i) jf<<","; jf<<"\"0x"<<std::hex<<std::setw(8)<<loop_pcs[i]<<"\""; }
                jf << std::dec << "],\n";
                jf << "  \"loop_size\":" << loop_pcs.size() << ",\n";
                jf << "  \"branch_hot\":\"0x" << std::hex << std::setw(8) << branch_pc_hot << std::dec << "\",\"branch_count\":" << bhot << ",\n";
                jf << "  \"branch_mnemonic\":\"" << (pc_mn.count(branch_pc_hot)?pc_mn[branch_pc_hot]:"") << "\",\n";
                jf << "  \"iterations_estimate\":" << (loop_pcs.empty()?0: hot_cnt) << ",\n";
                jf << "  \"modified_regs\":[";
                {bool f=true; for(int r: modified_regs){ if(!f) jf<<","; jf<<r; f=false; }} jf << "],\n";
                jf << "  \"invariant_regs\":[";
                {bool f=true; for(int r: invariant_regs){ if(!f) jf<<","; jf<<r; f=false; }} jf << "],\n";
                jf << "  \"mem_pages_accessed\":[";
                {bool f=true; for(uint32_t a: mem_addrs){ if(!f) jf<<","; jf<<"\"0x"<<std::hex<<std::setw(8)<<a<<"\""; f=false; }} jf << std::dec << "],\n";
                jf << "  \"total_trace\":" << dt.size() << ",\n";
                jf << "  \"pc_histogram\":{";
                {bool f=true; for(auto &kv: pc_count){ if(!f) jf<<","; jf<<"\"0x"<<std::hex<<std::setw(8)<<kv.first<<"\":"<<std::dec<<kv.second; f=false; }}
                jf << "}\n";
                jf << "}\n";
                std::cout << "    [OK] Wrote " << loop_path.string() << "\n";
            }
        }

        runtime::shutdown_memory();
    }
    // Generate dynamic_code_regions.json
    {
        std::filesystem::path dc_path = opt.output_dir / "dynamic_code_regions.json";
        std::ofstream jf(dc_path);
        if (jf.is_open()) {
            auto regions = runtime::DynamicCodeManager::instance().regions();
            jf << "[\n";
            for (size_t i = 0; i < regions.size(); ++i) {
                auto &r = regions[i];
                jf << "  {\"source\":" << r.source
                   << ",\"destination\":" << r.destination
                   << ",\"size\":" << r.size
                   << ",\"permissions\":\"" << r.permissions << "\""
                   << ",\"is_thumb\":" << (r.is_thumb?"true":"false")
                   << ",\"discovered_functions\":" << r.discovered_functions
                   << ",\"discovered_blocks\":" << r.discovered_blocks
                   << ",\"execution_count\":" << r.execution_count
                   << ",\"write_count\":" << r.write_count
                   << ",\"generation\":" << r.generation
                   << "}";
                if (i + 1 < regions.size()) jf << ",";
                jf << "\n";
            }
            jf << "]\n";
            std::cout << "    [OK] Wrote " << dc_path.string() << " (" << regions.size() << " regions)\n";
        }
    }
    // Generate subsystem_access.json
    {
        std::filesystem::path sa_path = opt.output_dir / "subsystem_access.json";
        std::ofstream jf(sa_path);
        if (jf.is_open()) {
            jf << runtime::SubsystemAccessTracker::instance().to_json() << "\n";
            std::cout << "    [OK] Wrote " << sa_path.string() << "\n";
        }
    }

    // Generate project_map.json - single source of truth linking all project data
    {
        std::filesystem::path mp_path = opt.output_dir / "project_map.json";
        std::ofstream jf(mp_path);
        if (jf.is_open()) {
            auto &bs = runtime::bus_stats();
            auto &fn = functions;
            auto &bb = basic_blocks;

            jf << "{\n";
            jf << "  \"functions\": " << fn.size() << ",\n";
            jf << "  \"basic_blocks\": " << bb.size() << ",\n";
            jf << "  \"executed_functions\": 0,\n";
            jf << "  \"executed_blocks\": 0,\n";

            jf << "  \"dynamic_regions\": " << runtime::DynamicCodeManager::instance().regions().size() << ",\n";
            jf << "  \"itcm_writes\": " << bs.itcm_writes << ",\n";
            jf << "  \"ram_writes\": " << bs.ram_writes << ",\n";
            jf << "  \"cp15_reads\": " << bs.cp15_reads << ",\n";
            jf << "  \"cp15_writes\": " << bs.cp15_writes << ",\n";
            jf << "  \"swi_count\": " << bs.swi_count << ",\n";
            jf << "  \"instructions_executed\": 0,\n";
            jf << "  \"hot_pc\": \"0x00000000\",\n";

            jf << "  \"callgraph\": \"see callgraph.json\",\n";
            jf << "  \"memory_regions\": [\n";
            jf << "    {\"base\": \"0x00000000\", \"size\": \"32KB\", \"type\": \"ITCM\", \"dynamic\": true},\n";
            jf << "    {\"base\": \"0x02000000\", \"size\": \"4MB\", \"type\": \"Main RAM\"},\n";
            jf << "    {\"base\": \"0x027E0000\", \"size\": \"16KB\", \"type\": \"DTCM\"},\n";
            jf << "    {\"base\": \"0x04000000\", \"size\": \"64KB\", \"type\": \"IO\"},\n";
            jf << "    {\"base\": \"0x06000000\", \"size\": \"VRAM\"},\n";
            jf << "    {\"base\": \"0xFFFF0000\", \"size\": \"BIOS9\"}\n";
            jf << "  ],\n";

            jf << "  \"assets\": [],\n";
            jf << "  \"generated_cpp_size\": 0\n";
            jf << "}\n";

            std::cout << "    [OK] Wrote " << mp_path.string() << " (project map)\n";
        }
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();

    std::cout << "\n[+] Analysis complete in " << elapsed_ms << " ms.\n";
    std::cout << "    Output files generated in: " << opt.output_dir.string() << "\n";

    return 0;
}
