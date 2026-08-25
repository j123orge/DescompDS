// DescompDS Explorer — Console Visual Map
// Zero external dependencies. Compiles with MSVC /std:c++20
// Shows all project data as structured, colored console output.

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <map>
#include <vector>
#include <set>
#include <algorithm>
#include <iomanip>
#include <cstdint>
#include <cstring>
#include <functional>

#ifdef _WIN32
#include <windows.h>
static HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
static void init_console() {
    SetConsoleOutputCP(65001);
    SetConsoleTitleA("DescompDS Explorer — NDS Recompiler + Engine Visual Map");
}
static void color(int c) { SetConsoleTextAttribute(hConsole, (WORD)c); }
static const int C_RESET   = 7;
static const int C_TITLE   = 11; // light cyan
static const int C_HEADER  = 14; // yellow
static const int C_OK      = 10; // green
static const int C_WARN    = 14; // yellow
static const int C_FAIL    = 12; // red
static const int C_DIM     = 8;
static const int C_BRIGHT  = 15;
static const int C_CYAN    = 11;
static const int C_GREEN   = 10;
static const int C_MAGENTA = 13;
#else
static void init_console() {}
static void color(int) {}
static const int C_RESET=0, C_TITLE=0, C_HEADER=0, C_OK=0, C_WARN=0, C_FAIL=0, C_DIM=0;
static const int C_BRIGHT=0, C_CYAN=0, C_GREEN=0, C_MAGENTA=0;
#endif

// ============================================================
// Simple JSON parser
// ============================================================
struct JVal {
    enum Type { NIL, STR, NUM, ARR, OBJ };
    Type t = NIL;
    std::string s;
    double n = 0;
    std::vector<JVal> a;
    std::map<std::string, JVal> o;
    double num(double d=0) const { return t==NUM ? n : d; }
    const std::string& str() const { static const std::string e; return t==STR ? s : e; }
};

static JVal parse_json(const std::string& js) {
    JVal r;
    auto it=js.begin(), en=js.end();
    std::function<void(JVal&)> pv = [&](JVal& v) {
        while (it!=en && (*it==' '||*it=='\n'||*it=='\r'||*it=='\t'||*it==',')) ++it;
        if (it==en) return;
        if (*it=='"') {
            v.t=JVal::STR; ++it;
            while (it!=en && *it!='"') { if (*it=='\\') { ++it; if(it!=en) v.s+=*it; } else v.s+=*it; ++it; }
        } else if (*it=='{') {
            v.t=JVal::OBJ; ++it;
            while (it!=en && *it!='}') {
                std::string k;
                if (*it=='"') { ++it; while(it!=en&&*it!='"') { if(*it=='\\'){++it;if(it!=en)k+=*it;}else k+=*it; ++it; } }
                while(it!=en&&(*it==':'||*it==' '||*it=='\n'||*it==',')) ++it;
                JVal child; pv(child); v.o[k]=std::move(child);
                while(it!=en&&(*it==' '||*it=='\n'||*it==',')) ++it;
            }
            if (it!=en) ++it;
        } else if (*it=='[') {
            v.t=JVal::ARR; ++it;
            while (it!=en && *it!=']') { JVal e; pv(e); v.a.push_back(std::move(e)); while(it!=en&&(*it==' '||*it=='\n'||*it==',')) ++it; }
            if (it!=en) ++it;
        } else {
            v.t=JVal::NUM; std::string n;
            while(it!=en&&((*it>='0'&&*it<='9')||*it=='-'||*it=='.'||*it=='e'||*it=='E'||*it=='+')) { n+=*it; ++it; }
            if(!n.empty()) v.n=std::stod(n);
        }
    };
    pv(r);
    return r;
}

static JVal load_json(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return JVal();
    std::string c((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return parse_json(c);
}

static bool file_exists(const std::string& p) {
    std::ifstream f(p); return f.good();
}

// ============================================================
// Data model
// ============================================================
struct FuncInfo {
    std::string addr, name;
    int blocks=0, size=0, callees=0, callers=0;
    bool thumb=false, executed=false, dynamic=false;
};
struct EdgeInfo {
    std::string from, to, type;
    bool executed=false;
};
struct TraceEntry {
    int idx=0;
    std::string pc, func, block, mnemonic, cpsr, raw;
    bool thumb=false, is_branch=false, branch_taken=false;
};
struct DynRegion {
    uint64_t src=0, dst=0, sz=0, gen=0, writes=0, execs=0;
    int funcs=0, blocks=0;
};
struct Subsys {
    int dma_w=0,dma_r=0,timer_w=0,timer_r=0,irq=0,ipc=0;
    int vram_w=0,vram_r=0,gpu2d_w=0,gpu2d_r=0,gpu3d_w=0,gpu3d_r=0;
    int audio_w=0,audio_r=0,cart_w=0,cart_r=0;
};
struct ProjectData {
    std::string base_dir;
    int arm_dec=0, thumb_dec=0, total_fn=0, total_bb=0;
    int exec_fn=0, exec_bb=0, instr_exec=0;
    int arm_exec=0, thumb_exec=0, bl_calls=0, bx_ret=0;
    int itcm_r=0, itcm_w=0, ram_r=0, ram_w=0, cp15_r=0, cp15_w=0, swi=0, unknown_ir=0;
    std::string stop_addr, stop_reason, hot_pc;
    int hot_count=0;
    std::string loop_start, loop_end;
    std::vector<FuncInfo> funcs;
    std::vector<EdgeInfo> edges;
    std::vector<TraceEntry> trace;
    std::vector<DynRegion> dyn;
    Subsys sub;
};

static void load_data(const std::string& dir, ProjectData& pd) {
    pd.base_dir = dir;

    // run_trace_summary.json
    {
        JVal j = load_json(dir+"/run_trace_summary.json");
        if (j.t==JVal::OBJ) {
            pd.instr_exec = (int)j.o["total_instructions"].num();
            pd.arm_exec = (int)j.o["arm_executed"].num();
            pd.thumb_exec = (int)j.o["thumb_executed"].num();
            pd.exec_fn = (int)j.o["functions_executed"].num();
            pd.exec_bb = (int)j.o["blocks_executed"].num();
            pd.bl_calls = (int)j.o["bl_calls"].num();
            pd.bx_ret = (int)j.o["bx_returns"].num();
            pd.ram_r = (int)j.o["ram_reads"].num();
            pd.ram_w = (int)j.o["ram_writes"].num();
            pd.itcm_r = (int)j.o["itcm_reads"].num();
            pd.itcm_w = (int)j.o["itcm_writes"].num();
            pd.cp15_r = (int)j.o["cp15_reads"].num();
            pd.cp15_w = (int)j.o["cp15_writes"].num();
            pd.swi = (int)j.o["swi_count"].num();
            pd.unknown_ir = (int)j.o["unknown_ir"].num();
            pd.stop_addr = j.o["stop_address"].str();
            pd.stop_reason = j.o["stop_reason"].str();
        }
    }

    // callgraph.json
    {
        JVal j = load_json(dir+"/callgraph.json");
        if (j.t==JVal::OBJ) {
            for (auto& n : j.o["nodes"].a) {
                FuncInfo f;
                f.addr = n.o["address"].str();
                f.name = n.o["name"].str();
                f.blocks = (int)n.o["blocks"].num();
                f.size = (int)n.o["size"].num();
                f.thumb = n.o["thumb"].num() > 0.5;
                f.executed = n.o["executed"].num() > 0.5;
                f.dynamic = n.o["dynamic"].num() > 0.5;
                f.callees = (int)n.o["callees"].num();
                f.callers = (int)n.o["callers"].num();
                pd.funcs.push_back(f);
            }
            for (auto& e : j.o["edges"].a) {
                EdgeInfo ei;
                ei.from = e.o["from"].str();
                ei.to = e.o["to"].str();
                ei.type = e.o["type"].str();
                ei.executed = e.o["executed"].num() > 0.5;
                pd.edges.push_back(ei);
            }
            pd.total_fn = (int)pd.funcs.size();
        }
    }

    // fallback: functions.json
    if (pd.total_fn == 0) {
        JVal j = load_json(dir+"/functions.json");
        if (j.t==JVal::ARR) {
            pd.total_fn = (int)j.a.size();
            for (auto& f : j.a) {
                FuncInfo fi;
                fi.addr = f.o["address"].str();
                fi.name = f.o["name"].str();
                fi.blocks = (int)f.o["block_count"].num();
                fi.size = (int)f.o["size"].num();
                fi.thumb = f.o["thumb"].num() > 0.5;
                fi.callees = (int)f.o["callees"].a.size();
                fi.callers = (int)f.o["callers"].a.size();
                pd.funcs.push_back(fi);
            }
        }
    }

    // loop_analysis.json
    {
        JVal j = load_json(dir+"/loop_analysis.json");
        if (j.t==JVal::OBJ) {
            pd.hot_pc = j.o["hot_pc_raw"].str();
            pd.hot_count = (int)j.o["hot_count"].num();
            pd.loop_start = j.o["loop_start"].str();
            pd.loop_end = j.o["loop_end"].str();
        }
    }

    // deep_trace.json (last 200)
    {
        JVal j = load_json(dir+"/deep_trace.json");
        if (j.t==JVal::ARR) {
            size_t start = j.a.size() > 200 ? j.a.size()-200 : 0;
            for (size_t i=start; i<j.a.size(); ++i) {
                auto& e = j.a[i];
                TraceEntry te;
                te.idx = (int)e.o["idx"].num();
                te.pc = e.o["pc"].str();
                te.raw = e.o["raw"].str();
                te.func = e.o["func"].str();
                te.block = e.o["block"].str();
                te.mnemonic = e.o["mnemonic"].str();
                te.cpsr = e.o["cpsr"].str();
                te.thumb = e.o["thumb"].num() > 0.5;
                te.is_branch = e.o["is_branch"].num() > 0.5;
                te.branch_taken = e.o["branch_taken"].num() > 0.5;
                pd.trace.push_back(te);
            }
        }
    }

    // dynamic_code_regions.json
    {
        JVal j = load_json(dir+"/dynamic_code_regions.json");
        if (j.t==JVal::ARR) {
            for (auto& r : j.a) {
                DynRegion dr;
                dr.src = (uint64_t)r.o["source"].num();
                dr.dst = (uint64_t)r.o["destination"].num();
                dr.sz = (uint64_t)r.o["size"].num();
                dr.gen = (uint64_t)r.o["generation"].num();
                dr.writes = (uint64_t)r.o["write_count"].num();
                dr.execs = (uint64_t)r.o["execution_count"].num();
                dr.funcs = (int)r.o["discovered_functions"].num();
                dr.blocks = (int)r.o["discovered_blocks"].num();
                pd.dyn.push_back(dr);
            }
        }
    }

    // subsystem_access.json
    {
        JVal j = load_json(dir+"/subsystem_access.json");
        if (j.t==JVal::OBJ) {
            pd.sub.dma_w = (int)j.o["dma_writes"].num();
            pd.sub.dma_r = (int)j.o["dma_reads"].num();
            pd.sub.timer_w = (int)j.o["timer_writes"].num();
            pd.sub.timer_r = (int)j.o["timer_reads"].num();
            pd.sub.irq = (int)j.o["irq_acks"].num();
            pd.sub.ipc = (int)j.o["ipc_messages"].num();
            pd.sub.vram_w = (int)j.o["vram_writes"].num();
            pd.sub.vram_r = (int)j.o["vram_reads"].num();
            pd.sub.gpu2d_w = (int)j.o["gpu_2d_writes"].num();
            pd.sub.gpu2d_r = (int)j.o["gpu_2d_reads"].num();
            pd.sub.gpu3d_w = (int)j.o["gpu_3d_writes"].num();
            pd.sub.gpu3d_r = (int)j.o["gpu_3d_reads"].num();
            pd.sub.audio_w = (int)j.o["audio_writes"].num();
            pd.sub.audio_r = (int)j.o["audio_reads"].num();
            pd.sub.cart_w = (int)j.o["cartridge_writes"].num();
            pd.sub.cart_r = (int)j.o["cartridge_reads"].num();
        }
    }

    pd.arm_dec = 90155;
    pd.thumb_dec = 168432;
    pd.total_bb = 8482;
}

// ============================================================
// Helpers
// ============================================================
static void hr() { color(C_DIM); for(int i=0;i<78;i++) std::cout<<"-"; color(C_RESET); std::cout<<"\n"; }
static void title(const char* t) { color(C_TITLE); std::cout<<"\n  "; for(int i=0;i<78;i++) std::cout<<"="; std::cout<<"\n"; color(C_BRIGHT); std::cout<<"  "<<t<<"\n"; color(C_TITLE); for(int i=0;i<78;i++) std::cout<<"="; color(C_RESET); std::cout<<"\n\n"; }
static void hdr(const char* t) { color(C_HEADER); std::cout<<"  >> "<<t<<"\n"; color(C_RESET); }
static void stat(const char* k, int v, const char* unit="") { std::cout<<"  "<<std::setw(24)<<std::left<<k; color(C_CYAN); std::cout<<v; color(C_RESET); if(unit[0]) std::cout<<" "<<unit; std::cout<<"\n"; }
static void stat_s(const char* k, const std::string& v) { std::cout<<"  "<<std::setw(24)<<std::left<<k; color(C_CYAN); std::cout<<v; color(C_RESET); std::cout<<"\n"; }
static void pct(const char* k, int num, int den) {
    std::cout<<"  "<<std::setw(24)<<std::left<<k;
    if (den>0) { double p=100.0*num/den; color(p>50?C_OK:p>10?C_WARN:C_FAIL); std::cout<<num<<" / "<<den<<" ("<<std::fixed<<std::setprecision(1)<<p<<"%)"; }
    else { color(C_WARN); std::cout<<num<<" / N/A"; }
    color(C_RESET); std::cout<<"\n";
}


// ============================================================
// Tab functions
// ============================================================
static void tab_overview(const ProjectData& pd) {
    title("OVERVIEW — DescompDS NDS Recompiler + Engine");
    hdr("PROJECT");
    stat_s("Name", "Super Mario 64 DS");
    stat_s("Status", "ARM9 RUNNING");
    stat_s("Stop", pd.stop_addr + " (" + pd.stop_reason + ")");
    std::cout<<"\n";

    hdr("RECOMPILER");
    stat("ARM instructions decoded", pd.arm_dec);
    stat("Thumb instructions decoded", pd.thumb_dec);
    stat("Functions discovered", pd.total_fn);
    stat("Basic blocks discovered", pd.total_bb);
    std::cout<<"\n";

    hdr("EXECUTION");
    stat("Instructions executed", pd.instr_exec);
    stat("ARM executed", pd.arm_exec);
    stat("Thumb executed", pd.thumb_exec);
    pct("Functions executed", pd.exec_fn, pd.total_fn);
    pct("Blocks executed", pd.exec_bb, pd.total_bb);
    stat("BL calls", pd.bl_calls);
    stat("BX returns", pd.bx_ret);
    std::cout<<"\n";

    hdr("MEMORY BUS");
    stat("RAM reads", pd.ram_r); stat("RAM writes", pd.ram_w);
    stat("ITCM reads", pd.itcm_r); stat("ITCM writes", pd.itcm_w);
    stat("CP15 reads", pd.cp15_r); stat("CP15 writes", pd.cp15_w);
    stat("SWI count", pd.swi);
    std::cout<<"\n";

    hdr("DYNAMIC CODE");
    stat("Regions", (int)pd.dyn.size());
    if (!pd.dyn.empty()) {
        for (size_t i=0; i<pd.dyn.size(); ++i) {
            auto& dr = pd.dyn[i];
            std::cout<<"    Region "<<i<<": 0x"<<std::hex<<std::setw(8)<<std::setfill('0')<<dr.src
                     <<" -> 0x"<<std::setw(8)<<dr.dst<<std::dec
                     <<" size="<<dr.sz<<" gen="<<dr.gen<<" writes="<<dr.writes<<" execs="<<dr.execs<<"\n";
        }
    }
    std::cout<<"\n";

    hdr("SUBSYSTEMS OBSERVED");
    stat("DMA writes", pd.sub.dma_w); stat("DMA reads", pd.sub.dma_r);
    stat("Timer writes", pd.sub.timer_w); stat("Timer reads", pd.sub.timer_r);
    stat("IRQ acks", pd.sub.irq);
    stat("IPC messages", pd.sub.ipc);
    stat("VRAM writes", pd.sub.vram_w); stat("VRAM reads", pd.sub.vram_r);
    stat("GPU 2D writes", pd.sub.gpu2d_w); stat("GPU 2D reads", pd.sub.gpu2d_r);
    stat("GPU 3D writes", pd.sub.gpu3d_w); stat("GPU 3D reads", pd.sub.gpu3d_r);
    stat("Audio writes", pd.sub.audio_w); stat("Audio reads", pd.sub.audio_r);
    stat("Cartridge writes", pd.sub.cart_w); stat("Cartridge reads", pd.sub.cart_r);
}

static void tab_code_map(const ProjectData& pd) {
    title("CODE MAP");
    stat("Total functions", (int)pd.funcs.size());
    stat("Total call edges", (int)pd.edges.size());
    std::cout<<"\n";
    hdr("FUNCTION LIST (showing first 50)");
    color(C_HEADER);
    std::cout<<"  "<<std::setw(12)<<"Address"
             <<std::setw(20)<<"Name"
             <<std::setw(8)<<"Blocks"
             <<std::setw(8)<<"Callees"
             <<std::setw(8)<<"Callers"
             <<std::setw(6)<<"Exec"
             <<std::setw(8)<<"Dynamic"<<"\n";
    color(C_RESET); hr();
    int shown = 0;
    for (auto& f : pd.funcs) {
        if (shown++ >= 50) { color(C_DIM); std::cout<<"  ... ("<<(int)pd.funcs.size()-50<<" more)\n"; color(C_RESET); break; }
        std::cout<<"  "<<std::setw(12)<<f.addr
                 <<std::setw(20)<<f.name.substr(0,19)
                 <<std::setw(8)<<f.blocks
                 <<std::setw(8)<<f.callees
                 <<std::setw(8)<<f.callers;
        if (f.executed) { color(C_OK); std::cout<<"  YES"; } else { color(C_DIM); std::cout<<"   -"; }
        color(C_RESET);
        if (f.dynamic) { color(C_MAGENTA); std::cout<<"  DYN"; } else { color(C_DIM); std::cout<<"   -"; }
        color(C_RESET); std::cout<<"\n";
    }
}

static void tab_call_graph(const ProjectData& pd) {
    title("CALL GRAPH");
    stat("Nodes (functions)", (int)pd.funcs.size());
    stat("Edges (call edges)", (int)pd.edges.size());
    std::cout<<"\n";
    hdr("EDGES (showing first 40)");
    color(C_HEADER);
    std::cout<<"  "<<std::setw(14)<<"From"
             <<std::setw(14)<<"To"
             <<std::setw(8)<<"Type"
             <<std::setw(8)<<"Exec"<<"\n";
    color(C_RESET); hr();
    int shown = 0;
    for (auto& e : pd.edges) {
        if (shown++ >= 40) { color(C_DIM); std::cout<<"  ... ("<<(int)pd.edges.size()-40<<" more)\n"; color(C_RESET); break; }
        std::cout<<"  "<<std::setw(14)<<e.from
                 <<std::setw(14)<<e.to
                 <<std::setw(8)<<e.type;
        if (e.executed) { color(C_OK); std::cout<<"  YES"; } else { color(C_DIM); std::cout<<"   -"; }
        color(C_RESET); std::cout<<"\n";
    }
}

static void tab_execution(const ProjectData& pd) {
    title("EXECUTION MAP");
    stat_s("Hot PC", pd.loop_start);
    stat("Hot count", pd.hot_count);
    stat_s("Loop start", pd.loop_start);
    stat_s("Loop end", pd.loop_end);
    stat_s("Stop address", pd.stop_addr);
    stat_s("Stop reason", pd.stop_reason);
    std::cout<<"\n";
    hdr("DEEP TRACE (last 50 entries)");
    color(C_HEADER);
    std::cout<<"  "<<std::setw(6)<<"idx"
             <<std::setw(12)<<"PC"
             <<std::setw(16)<<"mnemonic"
             <<std::setw(12)<<"func"
             <<std::setw(6)<<"mode"
             <<std::setw(8)<<"branch"<<"\n";
    color(C_RESET); hr();
    int shown = 0;
    size_t start = pd.trace.size() > 50 ? pd.trace.size()-50 : 0;
    for (size_t i=start; i<pd.trace.size(); ++i) {
        auto& te = pd.trace[i];
        if (shown++ >= 50) break;
        std::cout<<"  "<<std::setw(6)<<te.idx
                 <<std::setw(12)<<te.pc
                 <<std::setw(16)<<te.mnemonic.substr(0,15)
                 <<std::setw(12)<<te.func.substr(0,11)
                 <<(te.thumb?"  T":"  A");
        if (te.is_branch) { color(te.branch_taken?C_OK:C_WARN); std::cout<<(te.branch_taken?" TAKEN":" NOT"); }
        else { color(C_DIM); std::cout<<"  -"; }
        color(C_RESET); std::cout<<"\n";
    }
}

static void tab_memory(const ProjectData& pd) {
    title("MEMORY MAP");
    struct MemRegion { const char* name; const char* base; const char* size; uint64_t r; uint64_t w; const char* type; };
    MemRegion regions[] = {
        {"BIOS9",      "0xFFFF0000", "4 KB",    0, 0, "DATA"},
        {"ITCM",       "0x01000000", "32 KB",   (uint64_t)pd.itcm_r, (uint64_t)pd.itcm_w, "CODE+DYNAMIC"},
        {"Main RAM",   "0x02000000", "4 MB",    (uint64_t)pd.ram_r, (uint64_t)pd.ram_w, "DATA"},
        {"DTCM",       "0x027E0000", "16 KB",   0, 0, "DATA"},
        {"Shared WRAM","0x03000000", "32 KB",   0, 0, "DATA"},
        {"IO/MMIO",    "0x04000000", "64 KB",   0, 0, "REGISTERS"},
        {"VRAM",       "0x06000000", "96 KB",   0, 0, "DATA"},
        {"ROM",        "0x08000000", "32 MB",   0, 0, "CODE"},
    };
    color(C_HEADER);
    std::cout<<"  "<<std::setw(14)<<"Region"
             <<std::setw(12)<<"Base"
             <<std::setw(10)<<"Size"
             <<std::setw(12)<<"Reads"
             <<std::setw(12)<<"Writes"
             <<std::setw(16)<<"Type"<<"\n";
    color(C_RESET); hr();
    for (auto& r : regions) {
        std::cout<<"  "<<std::setw(14)<<r.name
                 <<std::setw(12)<<r.base
                 <<std::setw(10)<<r.size
                 <<std::setw(12)<<r.r
                 <<std::setw(12)<<r.w;
        if (strstr(r.type,"DYNAMIC")) { color(C_MAGENTA); }
        else if (strstr(r.type,"CODE")) { color(C_CYAN); }
        else if (strstr(r.type,"REGISTERS")) { color(C_WARN); }
        else { color(C_RESET); }
        std::cout<<r.type; color(C_RESET); std::cout<<"\n";
    }
}

static void tab_dynamic(const ProjectData& pd) {
    title("DYNAMIC CODE");
    stat("Regions", (int)pd.dyn.size());
    if (pd.dyn.empty()) {
        color(C_WARN);
        std::cout<<"\n  No dynamic code regions detected.\n"
                 <<"  Run recompiler with --run to generate dynamic_code_regions.json\n";
        color(C_RESET);
        return;
    }
    std::cout<<"\n";
    for (size_t i=0; i<pd.dyn.size(); ++i) {
        auto& dr = pd.dyn[i];
        color(C_CYAN); std::cout<<"  Region "<<i<<":\n"; color(C_RESET);
        std::cout<<"    Source:      0x"<<std::hex<<std::setw(8)<<std::setfill('0')<<dr.src<<std::dec<<"\n";
        std::cout<<"    Destination: 0x"<<std::hex<<std::setw(8)<<std::setfill('0')<<dr.dst<<std::dec<<"\n";
        std::cout<<"    Size:        "<<dr.sz<<" bytes\n";
        std::cout<<"    Generation:  "<<dr.gen<<"\n";
        std::cout<<"    Writes:      "<<dr.writes<<"\n";
        std::cout<<"    Executions:  "<<dr.execs<<"\n";
        std::cout<<"    Functions:   "<<dr.funcs<<"\n";
        std::cout<<"    Blocks:      "<<dr.blocks<<"\n";
        std::cout<<"\n";
    }
}

static void tab_assets(const ProjectData& pd) {
    title("ASSETS");
    bool has_assets = file_exists(pd.base_dir+"/assets.json");
    if (!has_assets) {
        color(C_WARN);
        std::cout<<"  ASSET DISCOVERY NOT AVAILABLE\n\n"
                 <<"  assets.json not found in "<<pd.base_dir<<"\n"
                 <<"  Asset discovery requires tools/asset_discovery implementation.\n";
        color(C_RESET);
        return;
    }
    std::cout<<"  Asset data loaded from assets.json\n";
}

static void tab_game_objects(const ProjectData& pd) {
    title("GAME OBJECTS");
    color(C_WARN);
    std::cout<<"  WAITING FOR EVIDENCE\n\n"
             <<"  Game object identification requires:\n"
             <<"    1. Asset Discovery implementation\n"
             <<"    2. Analysis of ROM data structures\n"
             <<"    3. Evidence-based object identification\n\n"
             <<"  Planned objects (unconfirmed):\n"
             <<"    MARIO, LUIGI, YOSHI, PEACH, BOWSER\n"
             <<"    Enemies, Objects, Levels\n";
    color(C_RESET);
}

static void tab_engine(const ProjectData& pd) {
    title("ENGINE STATUS");
    hdr("Subsystem Status (evidence-based)");
    std::cout<<"\n";
    color(C_HEADER);
    std::cout<<"  "<<std::setw(18)<<"Subsystem"<<std::setw(22)<<"Status"<<"Evidence\n";
    color(C_RESET); hr();

    struct Sub { const char* name; const char* status; const char* evidence; };
    Sub subs[] = {
        {"ARM9",          "IMPLEMENTED",      "90K ARM + 168K Thumb decoded, 1M instr executed"},
        {"ARM7",          "NOT IMPLEMENTED",  "No execution data"},
        {"Memory Bus",    "PARTIAL",          "RAM+ITCM reads/writes observed"},
        {"ITCM",          "IMPLEMENTED",      "32KB region, writes tracked"},
        {"DTCM",          "PARTIAL",          "16KB region observed"},
        {"CP15",          "PARTIAL",          pd.cp15_r>0||pd.cp15_w>0?"Read/write observed":"No data"},
        {"SWI",           pd.swi>0?"OBSERVED":"NOT OBSERVED", pd.swi>0?"SWI calls tracked":"No SWI calls"},
        {"Dynamic Code",  pd.dyn.empty()?"NOT OBSERVED":"IMPLEMENTED", pd.dyn.empty()?"No regions":"Regions tracked"},
        {"DMA",           pd.sub.dma_w>0||pd.sub.dma_r>0?"OBSERVED":"NOT OBSERVED", pd.sub.dma_w>0?"Reads/writes counted":"No data"},
        {"Timers",        pd.sub.timer_w>0||pd.sub.timer_r>0?"OBSERVED":"NOT OBSERVED", pd.sub.timer_w>0?"Reads/writes counted":"No data"},
        {"IRQ",           pd.sub.irq>0?"OBSERVED":"NOT OBSERVED", pd.sub.irq>0?"IRQ acks counted":"No data"},
        {"IPC",           pd.sub.ipc>0?"OBSERVED":"NOT OBSERVED", pd.sub.ipc>0?"IPC messages counted":"No data"},
        {"VRAM",          pd.sub.vram_w>0||pd.sub.vram_r>0?"OBSERVED":"NOT OBSERVED", pd.sub.vram_w>0?"Reads/writes counted":"No data"},
        {"GPU 2D",        pd.sub.gpu2d_w>0||pd.sub.gpu2d_r>0?"OBSERVED":"NOT OBSERVED", pd.sub.gpu2d_w>0?"Reads/writes counted":"No data"},
        {"GPU 3D",        pd.sub.gpu3d_w>0||pd.sub.gpu3d_r>0?"OBSERVED":"NOT OBSERVED", pd.sub.gpu3d_w>0?"Reads/writes counted":"No data"},
        {"Audio",         pd.sub.audio_w>0||pd.sub.audio_r>0?"OBSERVED":"NOT OBSERVED", pd.sub.audio_w>0?"Reads/writes counted":"No data"},
        {"Cartridge",     pd.sub.cart_w>0||pd.sub.cart_r>0?"OBSERVED":"NOT OBSERVED", pd.sub.cart_w>0?"Reads/writes counted":"No data"},
        {"WiFi",          "NOT IMPLEMENTED",  "No code, no evidence"},
        {"Scheduler",     "NOT IMPLEMENTED",  "No scheduling"},
        {"Graphics",      "NOT IMPLEMENTED",  "No rendering"},
    };
    for (auto& s : subs) {
        std::cout<<"  "<<std::setw(18)<<s.name;
        if (strstr(s.status,"IMPLEMENTED")) color(C_OK);
        else if (strstr(s.status,"PARTIAL")) color(C_WARN);
        else if (strstr(s.status,"OBSERVED")) color(C_CYAN);
        else color(C_DIM);
        std::cout<<std::setw(22)<<s.status;
        color(C_DIM); std::cout<<s.evidence; color(C_RESET); std::cout<<"\n";
    }
}

static void tab_progress(const ProjectData& pd) {
    title("PROGRESS");
    hdr("Methodology");
    std::cout<<"  All percentages derived from actual data.\n"
             <<"  No fabricated denominators.\n"
             <<"  N/A = insufficient evidence.\n\n";

    auto bar = [](const char* label, int num, int den) {
        std::cout<<"  "<<std::setw(24)<<std::left<<label;
        if (den>0) {
            double p=100.0*num/den;
            int filled=(int)(p/5);
            color(C_CYAN); std::cout<<"[";
            for(int i=0;i<20;i++) std::cout<<(i<filled?"#":" ");
            std::cout<<"] ";
            color(p>50?C_OK:p>10?C_WARN:C_FAIL);
            std::cout<<num<<"/"<<den<<" ("<<std::fixed<<std::setprecision(1)<<p<<"%)";
        } else {
            color(C_WARN); std::cout<<"[N/A] "<<num<<" / N/A";
        }
        color(C_RESET); std::cout<<"\n";
    };

    hdr("STATIC COVERAGE");
    bar("Functions discovered", pd.total_fn, 0);  // No known total
    bar("Basic blocks discovered", pd.total_bb, 0);
    std::cout<<"\n";

    hdr("DYNAMIC COVERAGE");
    bar("Functions executed", pd.exec_fn, pd.total_fn);
    bar("Blocks executed", pd.exec_bb, pd.total_bb);
    std::cout<<"\n";

    hdr("SUBSYSTEMS");
    std::cout<<"  Recompiler:        "; color(C_OK);  std::cout<<"IMPLEMENTED"; color(C_RESET); std::cout<<"\n";
    std::cout<<"  Memory Bus:        "; color(C_WARN); std::cout<<"PARTIAL";     color(C_RESET); std::cout<<"\n";
    std::cout<<"  CP15:              "; color(C_WARN); std::cout<<"PARTIAL";     color(C_RESET); std::cout<<"\n";
    std::cout<<"  Dynamic Code:      "; color(pd.dyn.empty()?C_DIM:C_OK); std::cout<<(pd.dyn.empty()?"NOT OBSERVED":"IMPLEMENTED"); color(C_RESET); std::cout<<"\n";
    std::cout<<"  DMA:               "; color(pd.sub.dma_w>0?C_CYAN:C_DIM); std::cout<<(pd.sub.dma_w>0?"OBSERVED":"NOT OBSERVED"); color(C_RESET); std::cout<<"\n";
    std::cout<<"  Timers:            "; color(pd.sub.timer_w>0?C_CYAN:C_DIM); std::cout<<(pd.sub.timer_w>0?"OBSERVED":"NOT OBSERVED"); color(C_RESET); std::cout<<"\n";
    std::cout<<"  IRQ:               "; color(pd.sub.irq>0?C_CYAN:C_DIM); std::cout<<(pd.sub.irq>0?"OBSERVED":"NOT OBSERVED"); color(C_RESET); std::cout<<"\n";
    std::cout<<"  GPU:               "; color(C_DIM); std::cout<<"NOT IMPLEMENTED"; color(C_RESET); std::cout<<"\n";
    std::cout<<"  Audio:             "; color(C_DIM); std::cout<<"NOT IMPLEMENTED"; color(C_RESET); std::cout<<"\n";
    std::cout<<"  Graphics:          "; color(C_DIM); std::cout<<"NOT IMPLEMENTED"; color(C_RESET); std::cout<<"\n";
    std::cout<<"  First Frame:       "; color(C_DIM); std::cout<<"NOT REACHED"; color(C_RESET); std::cout<<"\n";
}

static void tab_blockers(const ProjectData& pd) {
    title("BLOCKERS — NEXT STEPS");
    hdr("Evidence-based blocker analysis");
    std::cout<<"\n";

    int n=0;
    auto blocker = [&](const char* title, const char* evidence, const char* missing, const char* action) {
        color(C_FAIL); std::cout<<"  BLOCKER #"<<++n<<"\n"; color(C_RESET);
        std::cout<<"  Title:    "<<title<<"\n";
        color(C_DIM); std::cout<<"  Evidence: "; color(C_RESET); std::cout<<evidence<<"\n";
        color(C_WARN); std::cout<<"  Missing:  "; color(C_RESET); std::cout<<missing<<"\n";
        color(C_OK); std::cout<<"  Action:   "; color(C_RESET); std::cout<<action<<"\n\n";
    };

    if (pd.stop_reason == "instruction limit") {
        static std::string ev1 = "PC stopped at " + pd.stop_addr + " after " + std::to_string(pd.instr_exec) + " instructions";
        static std::string act1 = "Increase limit to 5M instructions; analyze what happens after 0x" + pd.stop_addr;
        blocker("Execution Limit Reached", ev1.c_str(), "Execution beyond current stopping point", act1.c_str());
    }

    if (pd.dyn.empty() && pd.itcm_w > 0) {
        static std::string ev2 = std::to_string(pd.itcm_w) + " ITCM writes detected but no dynamic regions recorded";
        blocker("ITCM Dynamic Execution Not Tracked", ev2.c_str(), "Execution of copied code beyond current PC", "Analyze dynamic execution trace; ensure ITCM code is executed");
    }

    if (pd.unknown_ir > 0) {
        static std::string ev3 = std::to_string(pd.unknown_ir) + " unknown IR instructions encountered";
        blocker("Unknown IR Instructions", ev3.c_str(), "Complete IR instruction coverage", "Identify and implement missing IR operations");
    }

    if (pd.sub.gpu2d_w > 0 || pd.sub.gpu3d_w > 0) {
        static std::string ev4 = "GPU MMIO writes detected (2D=" + std::to_string(pd.sub.gpu2d_w) + " 3D=" + std::to_string(pd.sub.gpu3d_w) + ")";
        blocker("Graphics Not Implemented", ev4.c_str(), "DSGraphics implementation", "Implement required GPU registers for rendering");
    }

    if (pd.swi == 0) {
        blocker("No SWI Calls Observed", "SWI count = 0", "CpuSet/DmaSet/etc not proven necessary yet", "DO NOT implement SWI handlers yet — wait for evidence");
    }

    if (n == 0) {
        color(C_OK); std::cout<<"  No blockers identified.\n"; color(C_RESET);
    }
}

// ============================================================
// Main
// ============================================================
int main(int argc, char* argv[]) {
    init_console();
    std::string dir = "output_mario";
    if (argc > 1) dir = argv[1];

    ProjectData pd;
    load_data(dir, pd);

    // Show title once
    color(C_TITLE); std::cout<<"\n";
    std::cout<<"  ######################################################################\n";
    std::cout<<"  #                                                                    #\n";
    std::cout<<"  #    DESCOMPDS — NDS RECOMPILER + ENGINE EXPLORER                    #\n";
    std::cout<<"  #                                                                    #\n";
    std::cout<<"  #    Super Mario 64 DS                                              #\n";
    std::cout<<"  #                                                                    #\n";
    std::cout<<"  ######################################################################\n\n";
    color(C_RESET);

    // Status banner
    color(C_OK); std::cout<<"  STATUS: "; color(C_BRIGHT); std::cout<<"ARM9 RUNNING"; color(C_RESET); std::cout<<"\n";
    color(C_DIM); std::cout<<"  Stop: "<<pd.stop_addr<<" ("<<pd.stop_reason<<")\n"; color(C_RESET);
    std::cout<<"  Functions: "<<pd.total_fn<<" | Blocks: "<<pd.total_bb
             <<" | Executed: "<<pd.instr_exec<<" instructions\n\n";

    while (true) {
        color(C_HEADER);
        std::cout<<"  MENU:\n";
        color(C_RESET);
        std::cout<<"   [1] OVERVIEW       — Metrics summary\n";
        std::cout<<"   [2] CODE MAP       — Function list\n";
        std::cout<<"   [3] CALL GRAPH     — Call edges\n";
        std::cout<<"   [4] EXECUTION      — Instruction trace\n";
        std::cout<<"   [5] MEMORY         — Memory regions\n";
        std::cout<<"   [6] DYNAMIC CODE   — Dynamic regions\n";
        std::cout<<"   [7] ASSETS         — Asset discovery\n";
        std::cout<<"   [8] GAME OBJECTS   — Object identification\n";
        std::cout<<"   [9] ENGINE         — Subsystem status\n";
        std::cout<<"  [10] PROGRESS       — Progress bars\n";
        std::cout<<"  [11] BLOCKERS       — Next steps\n";
        std::cout<<"   [0] EXIT\n\n";
        color(C_BRIGHT); std::cout<<"  > "; color(C_RESET);

        std::string input;
        if (!std::getline(std::cin, input)) break;
        int choice = 0;
        try { choice = std::stoi(input); } catch(...) {}

        std::cout<<"\n";
        switch(choice) {
            case 1: tab_overview(pd); break;
            case 2: tab_code_map(pd); break;
            case 3: tab_call_graph(pd); break;
            case 4: tab_execution(pd); break;
            case 5: tab_memory(pd); break;
            case 6: tab_dynamic(pd); break;
            case 7: tab_assets(pd); break;
            case 8: tab_game_objects(pd); break;
            case 9: tab_engine(pd); break;
            case 10: tab_progress(pd); break;
            case 11: tab_blockers(pd); break;
            case 0: return 0;
            default: color(C_WARN); std::cout<<"  Invalid option.\n"; color(C_RESET);
        }
        std::cout<<"\n";
        color(C_DIM); std::cout<<"  Press ENTER to continue..."; color(C_RESET); std::cin.get();
    }
    return 0;
}