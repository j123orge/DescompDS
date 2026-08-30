// DescompDS Explorer — Win32 GUI Visual Map
// Zero external dependencies. Native Win32 + Common Controls.

#define UNICODE
#define _UNICODE
#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <process.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <map>
#include <vector>
#include <algorithm>
#include <iomanip>
#include <cstdint>
#include <functional>

#include "bmd.h"
#include "romfile.h"
#include "d3d_view.h"

#pragma comment(lib, "comctl32.lib")

static const wchar_t* CLASS_NAME = L"DescompDSExplorer";
static HWND hMainWnd = nullptr;
static HWND hTabCtrl = nullptr;
static HWND hListView = nullptr;
static HWND hDetails = nullptr;
static HWND hRefView = nullptr;   // Function Explorer: references (callees/callers/blocks)
static HWND hGoEdit = nullptr;    // Function Explorer: go-to-address input
static HWND hGoButton = nullptr;  // Function Explorer: go button
static HWND hSearchEdit = nullptr;// Function Explorer: search/filter box
static int currentTab = 0;
static std::map<std::string, int> g_addr_to_func; // address -> funcs[] index
static int g_sel_func = -1;
// Function Explorer: filter/order + search + sort state
static std::vector<int> g_func_order;    // global func indices currently shown (filter+sort)
static std::wstring g_func_search;       // current search text
struct SortState { int col = -1; bool asc = true; };
static SortState g_sort_fn, g_sort_cm, g_sort_cg, g_sort_ex;
static std::vector<int> g_ov_target;          // target tab per OVERVIEW row
static std::vector<std::wstring> g_ov_desc;   // explanation per OVERVIEW row
static std::vector<int> g_codemap_order;  // CODE MAP: func indices in display order
static std::vector<int> g_edges_order;    // CALL GRAPH: edge indices in display order
static std::vector<int> g_trace_order;    // EXECUTION: trace indices in display order

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
            if (it!=en) ++it;
        } else if (*it=='{') {
            v.t=JVal::OBJ; ++it;
            while (it!=en && *it!='}') {
                while(it!=en&&(*it==' '||*it=='\n'||*it=='\r'||*it=='\t'||*it==',')) ++it;
                if (it!=en && *it=='}') break;
                std::string k;
                if (*it=='"') { ++it; while(it!=en&&*it!='"') { if(*it=='\\'){++it;if(it!=en)k+=*it;}else k+=*it; ++it; } if(it!=en) ++it; }
                while(it!=en&&(*it==':'||*it==' '||*it=='\n'||*it==','||*it=='\r'||*it=='\t')) ++it;
                JVal child; pv(child); v.o[k]=std::move(child);
                while(it!=en&&(*it==' '||*it=='\n'||*it==','||*it=='\r'||*it=='\t')) ++it;
            }
            if (it!=en) ++it;
        } else if (*it=='[') {
            v.t=JVal::ARR; ++it;
            while (it!=en && *it!=']') { JVal e; pv(e); v.a.push_back(std::move(e)); while(it!=en&&(*it==' '||*it=='\n'||*it==','||*it=='\r'||*it=='\t')) ++it; }
            if (it!=en) ++it;
        } else if (*it=='t') {
            v.t=JVal::NUM; v.n=1; while(it!=en&&(*it=='t'||*it=='r'||*it=='u'||*it=='e')) ++it;
        } else if (*it=='f') {
            v.t=JVal::NUM; v.n=0; while(it!=en&&(*it=='f'||*it=='a'||*it=='l'||*it=='s'||*it=='e')) ++it;
        } else if (*it=='n') {
            v.t=JVal::NIL; while(it!=en&&(*it=='n'||*it=='u'||*it=='l'||*it=='l')) ++it;
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
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return JVal();
    std::string c((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return parse_json(c);
}

static std::wstring s2ws(const std::string& s) {
    if(s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if(n<=0) return L"";
    std::wstring ws(n-1,0);
    MultiByteToWideChar(CP_UTF8,0,s.c_str(),-1,&ws[0],n);
    ws.resize(n-1);
    return ws;
}
static std::string ws2s(const std::wstring& ws) {
    if(ws.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8,0,ws.c_str(),-1,nullptr,0,nullptr,nullptr);
    if(n<=0) return "";
    std::string s(n-1,0);
    WideCharToMultiByte(CP_UTF8,0,ws.c_str(),-1,&s[0],n,nullptr,nullptr);
    s.resize(n-1);
    return s;
}

struct FuncInfo {
    std::string addr, name;
    int blocks=0, size=0, callees=0, callers=0;
    bool thumb=false, executed=false, dynamic=false;
    // real address lists from functions.json (for navigation)
    std::vector<std::string> block_addrs;
    std::vector<std::string> callee_addrs;
    std::vector<std::string> caller_addrs;
    std::vector<std::string> indirect_branches;
    std::vector<std::string> unknown_targets;
};
struct EdgeInfo { std::string from,to,type; bool executed=false; };
struct TraceEntry { int idx=0; std::string pc,func,block,mnemonic; bool thumb=false; };
struct DynRegion { uint64_t src=0,dst=0,sz=0; uint64_t writes=0,execs=0; int funcs=0,blocks=0; uint64_t gen=0; };
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
    int itcm_r=0,itcm_w=0, ram_r=0, ram_w=0;
    int callgraph_nodes=0;
    std::string stop_addr, stop_reason;
    std::vector<FuncInfo> funcs;
    std::vector<EdgeInfo> edges;
    std::vector<TraceEntry> trace;
    std::vector<DynRegion> dyn;
    Subsys sub;
};

static std::string file_size_str(const std::string& path){
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if(!f) return "N/A";
    std::streamsize sz = f.tellg();
    return std::to_string((size_t)sz) + " bytes";
}

static void load_data(const std::string& dir, ProjectData& pd, std::string& diag) {
    pd.base_dir = dir;
    diag.clear();
    diag += "DATA PATH: " + dir + "\n";
    char cwdbuf[MAX_PATH];
    if(GetCurrentDirectoryA(MAX_PATH, cwdbuf)) diag += std::string("CWD: ") + cwdbuf + "\n";
    wchar_t exeBuf[MAX_PATH];
    if(GetModuleFileNameW(nullptr, exeBuf, MAX_PATH)) diag += std::string("EXE: ") + ws2s(exeBuf) + "\n";

    auto check_file = [&](const std::string& name)->bool{
        std::string p = dir + "\\" + name;
        diag += "CHECK PATH: " + p + "\n";
        std::ifstream f(p);
        bool ok = f.good();
        diag += name + ": " + (ok?"OK":"MISSING") + "\n";
        if(ok) diag += "  size: " + file_size_str(p) + "\n";
        return ok;
    };

    check_file("run_trace_summary.json");
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
        pd.stop_addr = j.o["stop_address"].str();
        pd.stop_reason = j.o["stop_reason"].str();
    }
    diag += "run_trace_summary.json parsed: " + std::string(j.t==JVal::OBJ?"OK":"FAIL") + "\n";

    check_file("project_map.json");
    j = load_json(dir+"/project_map.json");
    if (j.t==JVal::OBJ) {
        pd.total_fn = (int)j.o["functions"].num();
        pd.total_bb = (int)j.o["basic_blocks"].num();
    }
    diag += "project_map.json parsed: " + std::string(j.t==JVal::OBJ?"OK":"FAIL") + "\n";

    // functions.json is the PRIMARY source for program functions (CODE MAP)
    check_file("functions.json");
    j = load_json(dir+"/functions.json");
    if (j.t==JVal::ARR) {
        for (auto& n : j.a) {
            FuncInfo f;
            f.addr = n.o["address"].str();
            f.name = n.o["name"].str();
            f.blocks = (int)n.o["block_count"].num();
            f.size = (int)n.o["size"].num();
            f.thumb = n.o["thumb"].num() > 0.5;
            f.callees = (int)n.o["callees"].a.size();
            f.callers = (int)n.o["callers"].a.size();
            for (auto& b : n.o["blocks"].a) f.block_addrs.push_back(b.str());
            for (auto& c : n.o["callees"].a) f.callee_addrs.push_back(c.str());
            for (auto& c : n.o["callers"].a) f.caller_addrs.push_back(c.str());
            for (auto& c : n.o["indirect_branches"].a) f.indirect_branches.push_back(c.str());
            for (auto& c : n.o["unknown_targets"].a) f.unknown_targets.push_back(c.str());
            pd.funcs.push_back(f);
        }
    }
    diag += "functions.json parsed: " + std::string(j.t==JVal::ARR?"OK":"FAIL") + ", funcs=" + std::to_string(pd.funcs.size()) + "\n";

    // callgraph.json = graph structure (nodes + edges) for CALL GRAPH tab
    check_file("callgraph.json");
    j = load_json(dir+"/callgraph.json");
    if (j.t==JVal::OBJ) {
        pd.callgraph_nodes = (int)j.o["nodes"].a.size();
        for (auto& e : j.o["edges"].a) {
            EdgeInfo ei;
            ei.from = e.o["from"].str();
            ei.to = e.o["to"].str();
            ei.type = e.o["type"].str();
            ei.executed = e.o["executed"].num() > 0.5;
            pd.edges.push_back(ei);
        }
    }
    diag += "callgraph.json parsed: " + std::string(j.t==JVal::OBJ?"OK":"FAIL") + ", nodes=" + std::to_string(pd.callgraph_nodes) + ", edges=" + std::to_string(pd.edges.size()) + "\n";

    check_file("dynamic_code_regions.json");
    j = load_json(dir+"/dynamic_code_regions.json");
    if (j.t==JVal::ARR) {
        for (auto& r : j.a) {
            DynRegion dr;
            dr.src = (uint64_t)r.o["source"].num();
            dr.dst = (uint64_t)r.o["destination"].num();
            dr.sz = (uint64_t)r.o["size"].num();
            dr.writes = (uint64_t)r.o["write_count"].num();
            dr.execs = (uint64_t)r.o["execution_count"].num();
            dr.funcs = (int)r.o["discovered_functions"].num();
            dr.blocks = (int)r.o["discovered_blocks"].num();
            dr.gen = (uint64_t)r.o["generation"].num();
            pd.dyn.push_back(dr);
        }
    }
    diag += "dynamic_code_regions.json parsed: " + std::string(j.t==JVal::ARR?"OK":"FAIL") + "\n";

    check_file("deep_trace.json");
    j = load_json(dir+"/deep_trace.json");
    if (j.t==JVal::ARR) {
        size_t start = j.a.size() > 500 ? j.a.size()-500 : 0;
        for (size_t i=start; i<j.a.size(); ++i) {
            auto& e = j.a[i];
            TraceEntry te;
            te.idx = (int)e.o["idx"].num();
            te.pc = e.o["pc"].str();
            te.func = e.o["func"].str();
            te.block = e.o["block"].str();
            te.mnemonic = e.o["mnemonic"].str();
            te.thumb = e.o["thumb"].num() > 0.5;
            pd.trace.push_back(te);
        }
    }
    diag += "deep_trace.json parsed: " + std::string(j.t==JVal::ARR?"OK":"FAIL") + "\n";

    check_file("subsystem_access.json");
    j = load_json(dir+"/subsystem_access.json");
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
    diag += "subsystem_access.json parsed: " + std::string(j.t==JVal::OBJ?"OK":"FAIL") + "\n";

    check_file("phase1_validation.json");
    j = load_json(dir+"/phase1_validation.json");
    if (j.t==JVal::OBJ) {
        pd.arm_dec = (int)j.o["arm9_instructions"].num();
        pd.thumb_dec = (int)j.o["thumb_instructions"].num();
    }
    diag += "phase1_validation.json parsed: " + std::string(j.t==JVal::OBJ?"OK":"FAIL") + "\n";

    diag += "--- SUMMARY ---\n";
    diag += "functions loaded (functions.json): " + std::to_string(pd.funcs.size()) + "\n";
    diag += "callgraph nodes loaded: " + std::to_string(pd.callgraph_nodes) + "\n";
    diag += "edges loaded: " + std::to_string(pd.edges.size()) + "\n";
    diag += "trace entries loaded: " + std::to_string(pd.trace.size()) + "\n";
    diag += "dynamic regions loaded: " + std::to_string(pd.dyn.size()) + "\n";
    diag += "total_fn: " + std::to_string(pd.total_fn) + "\n";
    diag += "total_bb: " + std::to_string(pd.total_bb) + "\n";
    diag += "instructions executed: " + std::to_string(pd.instr_exec) + "\n";
    diag += "itcm writes: " + std::to_string(pd.itcm_w) + "\n";

    // Write log next to exe
    wchar_t exePath[MAX_PATH];
    if(GetModuleFileNameW(nullptr, exePath, MAX_PATH)) {
        std::wstring exeDir = exePath;
        size_t p = exeDir.find_last_of(L"\\/");
        if(p!=std::wstring::npos) exeDir = exeDir.substr(0,p);
        std::ofstream lf(ws2s(exeDir + L"\\explorer_diag.log"), std::ios::binary);
        if(lf) lf << diag;
    }
}

static void add_list_column(HWND lv, int id, const wchar_t* name, int width) {
    LVCOLUMNW col{};
    col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    col.pszText = const_cast<LPWSTR>(name);
    col.cx = width;
    ListView_InsertColumn(lv, id, &col);
}

static void clear_list(HWND lv) {
    ListView_DeleteAllItems(lv);
    HWND hdr = (HWND)SendMessageW(lv, LVM_GETHEADER, 0, 0);
    int cols = (int)SendMessageW(hdr, HDM_GETITEMCOUNT, 0, 0);
    for(int i=0;i<cols;i++) ListView_DeleteColumn(lv, 0);
}

static std::wstring to_whex(uint64_t v){ wchar_t buf[32]; swprintf(buf,32,L"0x%08llX",(unsigned long long)v); return buf; }

static void set_lv(HWND lv, int item, int sub, const std::wstring& s){
    std::wstring tmp = s;
    ListView_SetItemText(lv, item, sub, &tmp[0]);
}

// --- Function Explorer helpers ---------------------------------------------

static ProjectData g_data;

static std::string norm_addr(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s;
}
static void build_addr_index() {
    g_addr_to_func.clear();
    for (int i = 0; i < (int)g_data.funcs.size(); i++)
        g_addr_to_func[norm_addr(g_data.funcs[i].addr)] = i;
}
// Resolve an address to a discovered function name, else mark it unknown.
static std::string resolve_func(const std::string& addr) {
    auto it = g_addr_to_func.find(norm_addr(addr));
    if (it != g_addr_to_func.end())
        return g_data.funcs[it->second].name + " [" + addr + "]";
    return addr + " [unknown]";
}
static void select_function(int idx) {
    g_sel_func = idx;
    if (idx < 0 || idx >= (int)g_data.funcs.size()) return;
    ListView_SetItemState(hListView, idx, LVIS_SELECTED|LVIS_FOCUSED, LVIS_SELECTED|LVIS_FOCUSED);
    ListView_EnsureVisible(hListView, idx, FALSE);
}
// forward declarations (defined later)
static void layout_controls(HWND hWnd);
static void populate_tab(int tabIdx, ProjectData& pd);
static void populate_functions(ProjectData& pd);
static void show_function_details(int idx);
static void goto_tab(int tabIdx) {
    if (!hTabCtrl) return;
    TabCtrl_SetCurSel(hTabCtrl, tabIdx);
    currentTab = tabIdx;
    HWND parent = hMainWnd ? hMainWnd : GetParent(hTabCtrl);
    layout_controls(parent);
    populate_tab(tabIdx, g_data);
}
// Open FUNCTIONS and select a specific global function index.
static void goto_function(int globalIdx) {
    goto_tab(11);
    // set search clear so the function is visible
    g_func_search = L"";
    populate_functions(g_data);
    int row = -1;
    for (int i = 0; i < (int)g_func_order.size(); i++) if (g_func_order[i] == globalIdx) { row = i; break; }
    if (row >= 0) {
        ListView_SetItemState(hListView, row, LVIS_SELECTED|LVIS_FOCUSED, LVIS_SELECTED|LVIS_FOCUSED);
        ListView_EnsureVisible(hListView, row, FALSE);
        show_function_details(globalIdx);
        g_sel_func = globalIdx;
    }
}
// Open FUNCTIONS at a function identified by address (or unknown).
static void goto_address(const std::string& addr) {
    auto it = g_addr_to_func.find(norm_addr(addr));
    if (it != g_addr_to_func.end()) goto_function(it->second);
    else SetWindowTextW(hDetails, (s2ws(addr) + L" is not a discovered function.").c_str());
}

// --- Selection details helpers (per tab) -----------------------------------

static void show_overview_item(int row) {
    if (row < 0 || row >= (int)g_ov_desc.size()) return;
    SetWindowTextW(hDetails, g_ov_desc[row].c_str());
}
static void show_edge_details(int row) {
    if (row < 0 || row >= (int)g_edges_order.size()) return;
    const EdgeInfo& e = g_data.edges[g_edges_order[row]];
    std::wstringstream ss;
    ss << L"FROM:\n  address: " << s2ws(e.from) << L"\n  func   : " << s2ws(resolve_func(e.from)) << L"\n\n";
    ss << L"TO:\n  address: " << s2ws(e.to) << L"\n  func   : " << s2ws(resolve_func(e.to)) << L"\n\n";
    ss << L"TYPE     : " << s2ws(e.type) << L"\n";
    ss << L"EXECUTED : " << (e.executed ? L"YES" : L"NO") << L"\n\n";
    ss << L"Double-click FROM/TO to open FUNCTIONS.";
    SetWindowTextW(hDetails, ss.str().c_str());
}
static void show_trace_details(int row) {
    if (row < 0 || row >= (int)g_trace_order.size()) return;
    const TraceEntry& t = g_data.trace[g_trace_order[row]];
    std::wstringstream ss;
    ss << L"Trace index : " << t.idx << L"\n";
    ss << L"PC          : " << s2ws(t.pc) << L"\n";
    ss << L"Mnemonic    : " << s2ws(t.mnemonic) << L"\n";
    ss << L"Function    : " << s2ws(t.func) << L"\n";
    ss << L"Basic Block : " << s2ws(t.block) << L"\n";
    ss << L"Thumb       : " << (t.thumb ? L"YES" : L"NO") << L"\n\n";
    ss << L"Note: only the last " << g_data.trace.size() << L" trace entries are loaded (deep_trace is larger).\n";
    ss << L"Double-click to open FUNCTIONS (if the function is discovered).";
    SetWindowTextW(hDetails, ss.str().c_str());
}
static void show_memory_region(int idx) {
    if (idx < 0 || idx > 4) return;
    static const wchar_t* names[] = {L"ITCM",L"RAM",L"DTCM",L"VRAM",L"ROM"};
    static const wchar_t* bases[] = {L"0x01000000",L"0x02000000",L"0x027E0000",L"0x06000000",L"0x08000000"};
    bool ok = (idx == 0 || idx == 1);
    int r = 0, w = 0;
    if (idx == 0) { r = g_data.itcm_r; w = g_data.itcm_w; }
    else if (idx == 1) { r = g_data.ram_r; w = g_data.ram_w; }
    std::wstringstream ss;
    ss << L"Region      : " << names[idx] << L"\n";
    ss << L"Base        : " << bases[idx] << L"\n";
    ss << L"Reads       : " << (ok ? std::to_wstring(r) : std::wstring(L"N/A")) << L"\n";
    ss << L"Writes      : " << (ok ? std::to_wstring(w) : std::wstring(L"N/A")) << L"\n";
    ss << L"Data source : " << (ok ? L"run_trace_summary.json" : L"none (not provided)") << L"\n\n";
    ss << L"N/A = data not available in the current analysis output.";
    SetWindowTextW(hDetails, ss.str().c_str());
}
static void show_dynamic_region(int idx) {
    if (idx < 0 || idx >= (int)g_data.dyn.size()) return;
    const DynRegion& d = g_data.dyn[idx];
    std::wstringstream ss;
    ss << L"Src        : " << to_whex(d.src) << L"\n";
    ss << L"Dst        : " << to_whex(d.dst) << L"\n";
    ss << L"Size       : " << d.sz << L"\n";
    ss << L"Writes     : " << d.writes << L"\n";
    ss << L"Executions : " << d.execs << L"\n";
    ss << L"Generation : " << d.gen << L"\n";
    ss << L"Functions  : " << d.funcs << L"\n";
    ss << L"Blocks     : " << d.blocks << L"\n";
    ss << L"Source     : dynamic_code_regions.json\n";
    SetWindowTextW(hDetails, ss.str().c_str());
}
static void show_engine_subsystem(int idx) {
    if (idx < 0 || idx > 6) return;
    struct S { const wchar_t* name; int w; int r; };
    S subs[] = {
        {L"DMA", g_data.sub.dma_w, g_data.sub.dma_r},
        {L"Timer", g_data.sub.timer_w, g_data.sub.timer_r},
        {L"VRAM", g_data.sub.vram_w, g_data.sub.vram_r},
        {L"GPU 2D", g_data.sub.gpu2d_w, g_data.sub.gpu2d_r},
        {L"GPU 3D", g_data.sub.gpu3d_w, g_data.sub.gpu3d_r},
        {L"Audio", g_data.sub.audio_w, g_data.sub.audio_r},
        {L"Cartridge", g_data.sub.cart_w, g_data.sub.cart_r},
    };
    std::wstringstream ss;
    ss << L"Subsystem : " << subs[idx].name << L"\n";
    ss << L"Writes    : " << subs[idx].w << L"\n";
    ss << L"Reads     : " << subs[idx].r << L"\n";
    ss << L"Source    : subsystem_access.json\n\n";
    ss << L"0 = no access counted; N/A would mean no data source (here the file exists).";
    SetWindowTextW(hDetails, ss.str().c_str());
}
static void show_progress_metric(int idx) {
    if (idx < 0 || idx > 6) return;
    static const wchar_t* names[] = {
        L"Instructions executed", L"Functions discovered", L"Basic blocks discovered",
        L"ARM decoded", L"Thumb decoded", L"ITCM writes", L"Dynamic regions"};
    std::wstringstream ss;
    ss << L"Metric : " << names[idx] << L"\n";
    ss << L"Value  : ";
    switch (idx) {
        case 0: ss << g_data.instr_exec; break;
        case 1: ss << g_data.funcs.size(); break;
        case 2: ss << g_data.total_bb; break;
        case 3: ss << g_data.arm_dec; break;
        case 4: ss << g_data.thumb_dec; break;
        case 5: ss << g_data.itcm_w; break;
        case 6: ss << g_data.dyn.size(); break;
    }
    ss << L"\nSource : real artifacts (functions.json / run_trace_summary.json / project_map.json / phase1_validation.json)\n";
    SetWindowTextW(hDetails, ss.str().c_str());
}
static void show_blocker(int idx) {
    if (idx < 0 || idx > 4) return;
    static const wchar_t* names[] = {
        L"Validator::ValidatesCleanCodeWithoutErrors",
        L"Validator::DetectsInvalidBranchTargets",
        L"Explorer visual implementation",
        L"GPU rendering",
        L"ARM7 execution"};
    static const wchar_t* status[] = {
        L"HISTORICAL FAILURE", L"HISTORICAL FAILURE", L"IN PROGRESS", L"NOT IMPLEMENTED", L"NOT IMPLEMENTED"};
    std::wstringstream ss;
    ss << L"Blocker : " << names[idx] << L"\n";
    ss << L"Status  : " << status[idx] << L"\n\n";
    ss << L"This reflects the current analysis/implementation state. No invented solution.";
    SetWindowTextW(hDetails, ss.str().c_str());
}

static void populate_overview(ProjectData& pd) {
    if (!hListView) return;
    clear_list(hListView);
    g_ov_target.clear(); g_ov_desc.clear();
    add_list_column(hListView,0,L"Metric",220);
    add_list_column(hListView,1,L"Value",160);
    struct Row { const wchar_t* name; std::wstring val; int target; const wchar_t* desc; };
    std::vector<Row> rows = {
        {L"Data", s2ws(pd.base_dir), -1, L"The analysis output directory."},
        {L"Functions", std::to_wstring(pd.total_fn), 11, L"Discovered functions (functions.json). Double-click to open FUNCTIONS."},
        {L"Basic Blocks", std::to_wstring(pd.total_bb), 1, L"Discovered basic blocks (project_map.json). Double-click to open CODE MAP."},
        {L"Callgraph Nodes", std::to_wstring(pd.callgraph_nodes), 2, L"Call graph nodes (callgraph.json). Double-click to open CALL GRAPH."},
        {L"Callgraph Edges", std::to_wstring(pd.edges.size()), 2, L"Call graph edges (callgraph.json). Double-click to open CALL GRAPH."},
        {L"Instructions", std::to_wstring(pd.instr_exec), 3, L"Instructions executed (run_trace_summary.json). Double-click to open EXECUTION."},
        {L"ITCM Writes", std::to_wstring(pd.itcm_w), 4, L"ITCM writes (run_trace_summary.json). Double-click to open MEMORY."},
        {L"Dynamic Regions", std::to_wstring(pd.dyn.size()), 5, L"Dynamic code regions (dynamic_code_regions.json). Double-click to open DYNAMIC CODE."},
        {L"Stop", s2ws(pd.stop_addr) + L" (" + s2ws(pd.stop_reason) + L")", -1, L"Where execution stopped (run_trace_summary.json)."},
    };
    int idx=0;
    for(auto& r : rows){
        LVITEMW it{};
        it.mask=LVIF_TEXT; it.iItem=idx; it.pszText=(LPWSTR)r.name;
        ListView_InsertItem(hListView,&it);
        set_lv(hListView,idx,1,r.val);
        g_ov_target.push_back(r.target);
        g_ov_desc.push_back(r.desc);
        idx++;
    }
    std::wstringstream ss;
    ss << L"DESCOMPDS EXPLORER\n\n";
    ss << L"Data: " << s2ws(pd.base_dir) << L"\n\n";
    ss << L"Select a row and double-click it to open the related tab.\n";
    ss << L"Functions: " << pd.total_fn << L"\n";
    ss << L"Basic Blocks: " << pd.total_bb << L"\n";
    ss << L"Callgraph Nodes: " << pd.callgraph_nodes << L"\n";
    ss << L"Callgraph Edges: " << pd.edges.size() << L"\n";
    ss << L"Instructions: " << pd.instr_exec << L"\n";
    ss << L"ITCM Writes: " << pd.itcm_w << L"\n";
    ss << L"Dynamic Regions: " << pd.dyn.size() << L"\n";
    SetWindowTextW(hDetails, ss.str().c_str());
}

static void populate_code_map(ProjectData& pd) {
    if (!hListView) return;
    if (g_codemap_order.size() != pd.funcs.size()) {
        g_codemap_order.clear(); g_sort_cm.col = -1;
        for (int i = 0; i < (int)pd.funcs.size(); i++) g_codemap_order.push_back(i);
    }
    clear_list(hListView);
    add_list_column(hListView,0,L"Address",100);
    add_list_column(hListView,1,L"Name",200);
    add_list_column(hListView,2,L"Blocks",60);
    add_list_column(hListView,3,L"Size",60);
    add_list_column(hListView,4,L"Callees",60);
    add_list_column(hListView,5,L"Callers",60);
    add_list_column(hListView,6,L"Exec",50);
    add_list_column(hListView,7,L"Dynamic",60);
    add_list_column(hListView,8,L"Thumb",50);
    int idx=0;
    for (int gi : g_codemap_order) {
        auto& f = pd.funcs[gi];
        LVITEMW it{};
        it.mask=LVIF_TEXT; it.iItem=idx; it.pszText=(LPWSTR)s2ws(f.addr).c_str();
        ListView_InsertItem(hListView,&it);
        set_lv(hListView,idx,1,s2ws(f.name));
        set_lv(hListView,idx,2,std::to_wstring(f.blocks));
        set_lv(hListView,idx,3,std::to_wstring(f.size));
        set_lv(hListView,idx,4,std::to_wstring(f.callees));
        set_lv(hListView,idx,5,std::to_wstring(f.callers));
        set_lv(hListView,idx,6,(f.executed?L"YES":L"NO"));
        set_lv(hListView,idx,7,(f.dynamic?L"YES":L"NO"));
        set_lv(hListView,idx,8,(f.thumb?L"YES":L"NO"));
        idx++;
    }
    std::wstring det = L"Functions: " + std::to_wstring(pd.funcs.size()) + L"\nSelect a function for details.";
    SetWindowTextW(hDetails, det.c_str());
}

static void populate_call_graph(ProjectData& pd) {
    if (!hListView) return;
    if (g_edges_order.size() != pd.edges.size()) {
        g_edges_order.clear(); g_sort_cg.col = -1;
        for (size_t i = 0; i < pd.edges.size(); i++) g_edges_order.push_back((int)i);
    }
    clear_list(hListView);
    add_list_column(hListView,0,L"From",100);
    add_list_column(hListView,1,L"To",100);
    add_list_column(hListView,2,L"Type",80);
    add_list_column(hListView,3,L"Exec",60);
    for (int ei : g_edges_order) {
        auto& e = pd.edges[ei];
        LVITEMW it{};
        it.mask=LVIF_TEXT; it.iItem=(int)it.iItem; it.pszText=(LPWSTR)s2ws(e.from).c_str();
        int r = (int)ListView_GetItemCount(hListView);
        it.iItem = r;
        ListView_InsertItem(hListView,&it);
        set_lv(hListView,r,1,s2ws(e.to));
        set_lv(hListView,r,2,s2ws(e.type));
        set_lv(hListView,r,3,(e.executed?L"YES":L"NO"));
    }
    std::wstring det = L"Edges: " + std::to_wstring(pd.edges.size()) + L" (nodes: " + std::to_wstring(pd.callgraph_nodes) + L")\nSelect an edge for details.";
    SetWindowTextW(hDetails, det.c_str());
}

static void populate_execution(ProjectData& pd) {
    if (!hListView) return;
    if (g_trace_order.size() != pd.trace.size()) {
        g_trace_order.clear(); g_sort_ex.col = -1;
        for (size_t i = 0; i < pd.trace.size(); i++) g_trace_order.push_back((int)i);
    }
    clear_list(hListView);
    add_list_column(hListView,0,L"Idx",60);
    add_list_column(hListView,1,L"PC",100);
    add_list_column(hListView,2,L"Mnemonic",120);
    add_list_column(hListView,3,L"Func",100);
    add_list_column(hListView,4,L"Block",100);
    add_list_column(hListView,5,L"Thumb",50);
    for (int ti : g_trace_order) {
        auto& t = pd.trace[ti];
        int r = (int)ListView_GetItemCount(hListView);
        LVITEMW it{};
        it.mask=LVIF_TEXT; it.iItem=r; it.pszText=(LPWSTR)std::to_wstring(t.idx).c_str();
        ListView_InsertItem(hListView,&it);
        set_lv(hListView,r,1,s2ws(t.pc));
        set_lv(hListView,r,2,s2ws(t.mnemonic));
        set_lv(hListView,r,3,s2ws(t.func));
        set_lv(hListView,r,4,s2ws(t.block));
        set_lv(hListView,r,5,(t.thumb?L"YES":L"NO"));
    }
    std::wstring det;
    det += L"Instructions executed: " + std::to_wstring(pd.instr_exec) + L"\n";
    det += L"Stop PC: " + s2ws(pd.stop_addr) + L"\n";
    det += L"Stop reason: " + s2ws(pd.stop_reason) + L"\n";
    det += L"ITCM writes: " + std::to_wstring(pd.itcm_w) + L"\n";
    det += L"Trace entries shown (last): " + std::to_wstring(pd.trace.size()) + L"\n";
    SetWindowTextW(hDetails, det.c_str());
}

static void populate_memory(ProjectData& pd) {
    if (!hListView) return;
    clear_list(hListView);
    add_list_column(hListView,0,L"Region",120);
    add_list_column(hListView,1,L"Base",100);
    add_list_column(hListView,2,L"Reads",80);
    add_list_column(hListView,3,L"Writes",80);
    // ok=true => real data available; ok=false => no data source => "N/A" (not 0)
    struct R{const wchar_t* name; const wchar_t* base; int r,w; bool ok;};
    R regs[]={
        {L"ITCM",L"0x01000000",pd.itcm_r,pd.itcm_w,true},
        {L"RAM",L"0x02000000",pd.ram_r,pd.ram_w,true},
        {L"DTCM",L"0x027E0000",0,0,false},
        {L"VRAM",L"0x06000000",0,0,false},
        {L"ROM",L"0x08000000",0,0,false},
    };
    for(int i=0;i<5;i++){
        LVITEMW it{};
        it.mask=LVIF_TEXT; it.iItem=i; it.pszText=(LPWSTR)regs[i].name;
        ListView_InsertItem(hListView,&it);
        set_lv(hListView,i,1,regs[i].base);
        set_lv(hListView,i,2,regs[i].ok?std::to_wstring(regs[i].r):std::wstring(L"N/A"));
        set_lv(hListView,i,3,regs[i].ok?std::to_wstring(regs[i].w):std::wstring(L"N/A"));
    }
    SetWindowTextW(hDetails, L"Memory regions from run_trace_summary.json.\nN/A where data not available.");
}

static void populate_dynamic(ProjectData& pd) {
    if (!hListView) return;
    clear_list(hListView);
    add_list_column(hListView,0,L"Src",100);
    add_list_column(hListView,1,L"Dst",100);
    add_list_column(hListView,2,L"Size",80);
    add_list_column(hListView,3,L"Writes",80);
    add_list_column(hListView,4,L"Exec",80);
    add_list_column(hListView,5,L"Gen",60);
    for(size_t i=0;i<pd.dyn.size();++i){
        auto& d=pd.dyn[i];
        LVITEMW it{};
        it.mask=LVIF_TEXT; it.iItem=(int)i; it.pszText=(LPWSTR)to_whex(d.src).c_str();
        ListView_InsertItem(hListView,&it);
        set_lv(hListView,(int)i,1,to_whex(d.dst));
        set_lv(hListView,(int)i,2,std::to_wstring(d.sz));
        set_lv(hListView,(int)i,3,std::to_wstring(d.writes));
        set_lv(hListView,(int)i,4,std::to_wstring(d.execs));
        set_lv(hListView,(int)i,5,std::to_wstring(d.gen));
    }
    std::wstring det = L"Dynamic code regions: " + std::to_wstring(pd.dyn.size()) + L"\nSelect region for details.";
    SetWindowTextW(hDetails, det.c_str());
}

static void populate_engine(ProjectData& pd) {
    if (!hListView) return;
    clear_list(hListView);
    add_list_column(hListView,0,L"Subsystem",150);
    add_list_column(hListView,1,L"Writes",80);
    add_list_column(hListView,2,L"Reads",80);
    struct S{const wchar_t* name; int w; int r;};
    S subs[]={
        {L"DMA",pd.sub.dma_w,pd.sub.dma_r},
        {L"Timer",pd.sub.timer_w,pd.sub.timer_r},
        {L"VRAM",pd.sub.vram_w,pd.sub.vram_r},
        {L"GPU 2D",pd.sub.gpu2d_w,pd.sub.gpu2d_r},
        {L"GPU 3D",pd.sub.gpu3d_w,pd.sub.gpu3d_r},
        {L"Audio",pd.sub.audio_w,pd.sub.audio_r},
        {L"Cartridge",pd.sub.cart_w,pd.sub.cart_r},
    };
    for(int i=0;i<7;i++){
        LVITEMW it{}; it.mask=LVIF_TEXT; it.iItem=i; it.pszText=(LPWSTR)subs[i].name;
        ListView_InsertItem(hListView,&it);
        set_lv(hListView,i,1,std::to_wstring(subs[i].w));
        set_lv(hListView,i,2,std::to_wstring(subs[i].r));
    }
    SetWindowTextW(hDetails, L"Subsystem access from subsystem_access.json. N/A = no data.");
}

static void populate_progress(ProjectData& pd) {
    if (!hListView) return;
    clear_list(hListView);
    add_list_column(hListView,0,L"Metric",220);
    add_list_column(hListView,1,L"Value",120);
    struct Row { const wchar_t* name; std::wstring val; };
    std::vector<Row> rows = {
        {L"Instructions executed", std::to_wstring(pd.instr_exec)},
        {L"Functions discovered", std::to_wstring(pd.funcs.size())},
        {L"Basic blocks discovered", std::to_wstring(pd.total_bb)},
        {L"ARM decoded", std::to_wstring(pd.arm_dec)},
        {L"Thumb decoded", std::to_wstring(pd.thumb_dec)},
        {L"ITCM writes", std::to_wstring(pd.itcm_w)},
        {L"Dynamic regions", std::to_wstring(pd.dyn.size())},
    };
    for(size_t i=0;i<rows.size();++i){
        LVITEMW it{}; it.mask=LVIF_TEXT; it.iItem=(int)i; it.pszText=(LPWSTR)rows[i].name;
        ListView_InsertItem(hListView,&it);
        set_lv(hListView,(int)i,1,rows[i].val);
    }
    SetWindowTextW(hDetails, L"Progress based on real artifacts. No fabricated data.");
}

static void populate_blockers(ProjectData& pd) {
    if (!hListView) return;
    clear_list(hListView);
    add_list_column(hListView,0,L"Blocker",300);
    add_list_column(hListView,1,L"Status",140);
    const wchar_t* items[][2] = {
        {L"Validator::ValidatesCleanCodeWithoutErrors", L"HISTORICAL FAILURE"},
        {L"Validator::DetectsInvalidBranchTargets", L"HISTORICAL FAILURE"},
        {L"Explorer visual implementation", L"IN PROGRESS"},
        {L"GPU rendering", L"NOT IMPLEMENTED"},
        {L"ARM7 execution", L"NOT IMPLEMENTED"},
    };
    for(int i=0;i<5;i++){
        LVITEMW it{}; it.mask=LVIF_TEXT; it.iItem=i; it.pszText=(LPWSTR)items[i][0];
        ListView_InsertItem(hListView,&it);
        set_lv(hListView,i,1,items[i][1]);
    }
    SetWindowTextW(hDetails, L"Blockers are evidence-based. Historical Validator failures preserved.");
}

// Loaded SM64DS model for the ASSETS tab (file[175] = Mario head/cap).
// --- Integrated ASSETS viewport --------------------------------------------

static bmd::Model g_asset_model;
static bool g_asset_model_loaded = false;
static HWND hViewport = nullptr;   // embedded D3D viewport (child of main window)
static int g_sel_asset = 0;        // selected asset row (persists across tab switches)
static int g_loaded_asset = -1;    // file index currently loaded in the viewport

struct AssetEntry { int file; std::string name; std::string type; };
static std::vector<AssetEntry> g_assets;

static std::string g_rom_path;

// Catalog of assets discovered in the ROM (models verified as BMD).
static void build_asset_catalog() {
    g_assets.clear();
    g_assets.push_back({175, "Mario head/cap (file[175])", "MODEL"});
    g_assets.push_back({1921, "Mario body (file[1921])", "MODEL"});
    g_assets.push_back({1933, "Mario body variant (file[1933])", "MODEL"});
    g_assets.push_back({190, "UI model (file[190])", "MODEL"});
    g_assets.push_back({112, "Audio SDAT (file[112])", "AUDIO"});
}

static bool load_model_file(int file_index, bmd::Model& out) {
    out = bmd::Model();
    if (g_rom_path.empty()) return false;
    std::vector<uint8_t> raw;
    if (!romfile::read_file(g_rom_path, file_index, raw)) return false;
    std::vector<uint8_t> dec = romfile::decompress(raw);
    if (dec.empty()) return false;
    std::string err;
    return bmd::parse_model(dec, out, err);
}

static void render_asset_in_viewport() {
    if (!hViewport) return;
    if (!d3dview::initialized()) d3dview::init(hViewport);
    if (g_asset_model_loaded) {
        d3dview::load_model(g_asset_model, 1);
        d3dview::focus_model();
    }
    InvalidateRect(hViewport, nullptr, FALSE);
}

static long count_prim_groups(const bmd::Model& m) {
    long c = 0; for (auto& g : m.groups) c += (long)g.prims.size(); return c;
}
static long count_triangles(const bmd::Model& m) {
    long tris = 0;
    for (auto& g : m.groups) for (auto& p : g.prims) {
        long n = (long)p.verts.size();
        if (p.type == bmd::PrimType::Triangles) tris += n / 3;
        else if (p.type == bmd::PrimType::TriangleStrip) tris += (n - 2 > 0 ? n - 2 : 0);
        else if (p.type == bmd::PrimType::Quads) tris += n / 2;
        else if (p.type == bmd::PrimType::QuadStrip) tris += (n - 2 > 0 ? n - 2 : 0);
    }
    return tris;
}
static std::wstring bbox_line(float minx,float miny,float minz,float maxx,float maxy,float maxz){
    std::wstringstream s;
    s << L"  Min: (" << minx << L", " << miny << L", " << minz << L")\n";
    s << L"  Max: (" << maxx << L", " << maxy << L", " << maxz << L")\n";
    s << L"  Size: (" << (maxx-minx) << L", " << (maxy-miny) << L", " << (maxz-minz) << L")\n";
    return s.str();
}

static void select_asset(int idx) {
    if (idx < 0 || idx >= (int)g_assets.size()) return;
    g_sel_asset = idx;
    const AssetEntry& a = g_assets[idx];
    std::wstringstream ss;
    ss << L"Asset: " << s2ws(a.name) << L"\n";
    ss << L"Type: " << s2ws(a.type) << L"\n";
    ss << L"ROM File: " << a.file << L"\n";
    if (a.type == "MODEL") {
        bool already = g_asset_model_loaded && (g_loaded_asset == a.file);
        if (!already) {
            if (!load_model_file(a.file, g_asset_model)) {
                g_asset_model_loaded = false;
                ss << L"\nFailed to parse model.\n";
                SetWindowTextW(hDetails, ss.str().c_str());
                return;
            }
            g_asset_model_loaded = true;
            g_loaded_asset = a.file;
        }
        ss << L"\nGeometry\n";
        ss << L"  Vertices: " << g_asset_model.gx_vertices << L"\n";
        ss << L"  Triangles: " << count_triangles(g_asset_model) << L"\n";
        ss << L"  GX Primitive Groups: " << count_prim_groups(g_asset_model) << L"\n";
        ss << L"  Bones: " << g_asset_model.bones.size() << L"\n";
        ss << L"  Materials: " << g_asset_model.materials.size() << L"\n";
        ss << L"  Textures: " << g_asset_model.textures.size() << L"\n";
        if (g_asset_model.has_bbox) {
            ss << L"\nRaw Bounds\n";
            ss << bbox_line(g_asset_model.minx, g_asset_model.miny, g_asset_model.minz,
                            g_asset_model.maxx, g_asset_model.maxy, g_asset_model.maxz);
        }
        if (g_asset_model.has_wbbox) {
            ss << L"\nWorld Bounds\n";
            ss << bbox_line(g_asset_model.wminx, g_asset_model.wminy, g_asset_model.wminz,
                            g_asset_model.wmaxx, g_asset_model.wmaxy, g_asset_model.wmaxz);
        }
        render_asset_in_viewport();
    } else {
        ss << L"\nNo 3D visualization available for this asset type.\n";
        ss << L"(view only properties)";
        if (hViewport) InvalidateRect(hViewport, nullptr, FALSE);
    }
    SetWindowTextW(hDetails, ss.str().c_str());
}

static void populate_assets(ProjectData& pd){
    if (hListView) clear_list(hListView);
    if (g_assets.empty()) build_asset_catalog();
    add_list_column(hListView, 0, L"Asset", 220);
    add_list_column(hListView, 1, L"Type", 80);
    add_list_column(hListView, 2, L"File", 60);
    int idx = 0;
    for (auto& a : g_assets) {
        LVITEMW it{}; it.mask = LVIF_TEXT; it.iItem = idx; it.pszText = (LPWSTR)s2ws(a.name).c_str();
        ListView_InsertItem(hListView, &it);
        set_lv(hListView, idx, 1, s2ws(a.type));
        set_lv(hListView, idx, 2, std::to_wstring(a.file));
        idx++;
    }
    // Re-select the saved asset (first open defaults to 0). select_asset skips reload
    // if the same model is already loaded.
    if (g_sel_asset < 0 || g_sel_asset >= (int)g_assets.size()) g_sel_asset = 0;
    if (!g_assets.empty()) {
        select_asset(g_sel_asset);
        ListView_SetItemState(hListView, g_sel_asset, LVIS_SELECTED|LVIS_FOCUSED, LVIS_SELECTED|LVIS_FOCUSED);
        ListView_EnsureVisible(hListView, g_sel_asset, FALSE);
    }
}
static void populate_game_objects(ProjectData& pd){
    if (hListView) clear_list(hListView);
    add_list_column(hListView, 0, L"Message", 500);
    LVITEMW it{}; it.mask=LVIF_TEXT; it.iItem=0; it.pszText=(LPWSTR)L"No game objects identified in current analysis.";
    ListView_InsertItem(hListView,&it);
    SetWindowTextW(hDetails, L"No game objects identified in current analysis.\n\nNothing is shown here because no object data has been produced yet — nothing is invented.");
}

static void show_function_details(int idx) {
    if (idx < 0 || idx >= (int)g_data.funcs.size()) return;
    const FuncInfo& f = g_data.funcs[idx];
    std::wstringstream ss;
    ss << L"Address      : " << s2ws(f.addr) << L"\n";
    ss << L"Name         : " << s2ws(f.name) << L"\n";
    ss << L"Architecture : " << (f.thumb ? L"Thumb" : L"ARM") << L"\n";
    ss << L"Size         : " << f.size << L" bytes\n";
    ss << L"Basic blocks : " << f.blocks << L"\n";
    ss << L"Callees      : " << f.callees << L"\n";
    ss << L"Callers      : " << f.callers << L"\n";
    if (!f.block_addrs.empty()) {
        ss << L"\nBasic Blocks:\n";
        for (auto& b : f.block_addrs) ss << L"  " << s2ws(b) << L"\n";
    } else {
        ss << L"\nBasic Blocks: not available\n";
    }
    // executed instructions from deep_trace (grouped by function), if any
    ss << L"\nExecuted instructions (deep_trace):\n";
    int hits = 0;
    for (auto& t : g_data.trace) {
        if (norm_addr(t.func) == norm_addr(f.addr)) {
            ss << L"  " << s2ws(t.pc) << L"  " << s2ws(t.mnemonic)
               << (t.thumb ? L"  (T)" : L"") << L"\n";
            if (++hits >= 200) { ss << L"  ... (truncated)\n"; break; }
        }
    }
    if (hits == 0) ss << L"  not available (function not present in deep_trace)\n";
    SetWindowTextW(hDetails, ss.str().c_str());

    // references view: blocks, callees, callers (navigable rows)
    if (hRefView) {
        clear_list(hRefView);
        add_list_column(hRefView, 0, L"Ref", 80);
        add_list_column(hRefView, 1, L"Address", 110);
        add_list_column(hRefView, 2, L"Target", 220);
        int r = 0;
        auto row = [&](const wchar_t* kind, const std::string& addr) {
            LVITEMW it{}; it.mask = LVIF_TEXT; it.iItem = r; it.pszText = (LPWSTR)kind;
            ListView_InsertItem(hRefView, &it);
            set_lv(hRefView, r, 1, s2ws(addr));
            set_lv(hRefView, r, 2, s2ws(resolve_func(addr)));
            r++;
        };
        for (auto& b : f.block_addrs) row(L"BLOCK", b);
        for (auto& c : f.callee_addrs) row(L"CALLEE", c);
        for (auto& c : f.caller_addrs) row(L"CALLER", c);
    }
}

static uint64_t parse_addr(const std::string& s) {
    uint64_t v = 0; const char* p = s.c_str();
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2;
    for (; *p; ++p) {
        v <<= 4;
        if (*p >= '0' && *p <= '9') v |= (uint64_t)(*p - '0');
        else if (*p >= 'a' && *p <= 'f') v |= (uint64_t)(*p - 'a' + 10);
        else if (*p >= 'A' && *p <= 'F') v |= (uint64_t)(*p - 'A' + 10);
        else return 0;
    }
    return v;
}
static std::wstring lower(std::wstring s) { std::transform(s.begin(), s.end(), s.begin(), ::tolower); return s; }
static void toggle_sort(SortState& s, int col) {
    if (s.col == col) s.asc = !s.asc; else { s.col = col; s.asc = true; }
}

// Build g_func_order = func indices matching g_func_search (empty search = all), sorted by g_sort_fn.
static void rebuild_func_list() {
    g_func_order.clear();
    std::wstring q = lower(g_func_search);
    for (int i = 0; i < (int)g_data.funcs.size(); i++) {
        const FuncInfo& f = g_data.funcs[i];
        if (q.empty()) { g_func_order.push_back(i); continue; }
        std::wstring name = lower(s2ws(f.name));
        std::wstring addr = lower(s2ws(f.addr));
        std::wstring addr_nox = (addr.size() > 2 && addr[1] == 'x') ? addr.substr(2) : addr;
        if (name.find(q) != std::wstring::npos || addr.find(q) != std::wstring::npos ||
            addr_nox.find(q) != std::wstring::npos)
            g_func_order.push_back(i);
    }
    if (g_sort_fn.col >= 0) {
        auto cmp = [&](int a, int b) -> bool {
            const FuncInfo& A = g_data.funcs[a]; const FuncInfo& B = g_data.funcs[b];
            int r = 0;
            switch (g_sort_fn.col) {
                case 0: r = (parse_addr(A.addr) < parse_addr(B.addr)) ? -1 : (parse_addr(A.addr) > parse_addr(B.addr) ? 1 : 0); break;
                case 1: r = A.name.compare(B.name); break;
                case 2: r = (int)A.thumb - (int)B.thumb; break;
                case 3: r = A.size - B.size; break;
                case 4: r = A.blocks - B.blocks; break;
                case 5: r = A.callees - B.callees; break;
                case 6: r = A.callers - B.callers; break;
                default: r = 0;
            }
            if (r == 0) r = (parse_addr(A.addr) < parse_addr(B.addr)) ? -1 : 1;
            return g_sort_fn.asc ? (r < 0) : (r > 0);
        };
        std::stable_sort(g_func_order.begin(), g_func_order.end(), cmp);
    }
}

static void populate_functions(ProjectData& pd) {
    rebuild_func_list();
    if (hListView) clear_list(hListView);
    add_list_column(hListView, 0, L"Address", 100);
    add_list_column(hListView, 1, L"Name", 200);
    add_list_column(hListView, 2, L"Arch", 60);
    add_list_column(hListView, 3, L"Size", 60);
    add_list_column(hListView, 4, L"Blocks", 60);
    add_list_column(hListView, 5, L"Callees", 60);
    add_list_column(hListView, 6, L"Callers", 60);
    int idx = 0;
    for (int gi : g_func_order) {
        const FuncInfo& f = pd.funcs[gi];
        LVITEMW it{}; it.mask = LVIF_TEXT; it.iItem = idx; it.pszText = (LPWSTR)s2ws(f.addr).c_str();
        ListView_InsertItem(hListView, &it);
        set_lv(hListView, idx, 1, s2ws(f.name));
        set_lv(hListView, idx, 2, (f.thumb ? L"Thumb" : L"ARM"));
        set_lv(hListView, idx, 3, std::to_wstring(f.size));
        set_lv(hListView, idx, 4, std::to_wstring(f.blocks));
        set_lv(hListView, idx, 5, std::to_wstring(f.callees));
        set_lv(hListView, idx, 6, std::to_wstring(f.callers));
        idx++;
    }
    if (hRefView) clear_list(hRefView);
    std::wstringstream ss;
    ss << L"Showing " << g_func_order.size() << L" of " << pd.funcs.size() << L".\n";
    ss << L"Select a function to inspect.\nClick a CALLEE/CALLER row (bottom) or type an address in Go to navigate.";
    SetWindowTextW(hDetails, ss.str().c_str());
    g_sel_func = -1;
}

static void navigate_to_address(const std::string& addr) {
    auto it = g_addr_to_func.find(norm_addr(addr));
    if (it != g_addr_to_func.end()) {
        select_function(it->second);
        show_function_details(it->second);
    } else {
        SetWindowTextW(hDetails, (s2ws(addr) + L" is not a discovered function.").c_str());
    }
}

static std::string g_tablog;
static void log_tab_items(int tab, ProjectData& pd){
    int items = hListView ? ListView_GetItemCount(hListView) : -1;
    g_tablog += "TAB " + std::to_string(tab) + " items=" + std::to_string(items) + "\n";
    // write to file
    wchar_t exePath[MAX_PATH];
    if(GetModuleFileNameW(nullptr, exePath, MAX_PATH)){
        std::wstring exeDir = exePath;
        size_t p = exeDir.find_last_of(L"\\/");
        if(p!=std::wstring::npos) exeDir = exeDir.substr(0,p);
        std::ofstream lf(ws2s(exeDir + L"\\explorer_tabs.log"), std::ios::app);
        if(lf) lf << "TAB " << tab << " items=" << items << "\n";
    }
}

static void populate_tab(int tabIdx, ProjectData& pd) {
    if (hViewport) ShowWindow(hViewport, tabIdx == 6 ? SW_SHOW : SW_HIDE);
    bool isFn = (tabIdx == 11);
    if (hRefView) ShowWindow(hRefView, isFn ? SW_SHOW : SW_HIDE);
    if (hGoEdit) ShowWindow(hGoEdit, isFn ? SW_SHOW : SW_HIDE);
    if (hGoButton) ShowWindow(hGoButton, isFn ? SW_SHOW : SW_HIDE);
    if (hSearchEdit) ShowWindow(hSearchEdit, isFn ? SW_SHOW : SW_HIDE);
    switch(tabIdx) {
        case 0: populate_overview(pd); break;
        case 1: populate_code_map(pd); break;
        case 2: populate_call_graph(pd); break;
        case 3: populate_execution(pd); break;
        case 4: populate_memory(pd); break;
        case 5: populate_dynamic(pd); break;
        case 6: populate_assets(pd); break;
        case 7: populate_game_objects(pd); break;
        case 8: populate_engine(pd); break;
        case 9: populate_progress(pd); break;
        case 10: populate_blockers(pd); break;
        case 11: populate_functions(pd); break;
        default: SetWindowTextW(hDetails, L"Tab content loaded from real JSON data.");
    }
    log_tab_items(tabIdx, pd);
}

// Embedded D3D viewport (child of the main Explorer window).
static LRESULT CALLBACK ViewportProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_PAINT: {
            d3dview::render();
            ValidateRect(hwnd, nullptr);
            return 0;
        }
        case WM_SIZE: {
            if (d3dview::initialized()) {
                d3dview::shutdown();
                d3dview::init(hwnd);
                if (g_asset_model_loaded) { d3dview::load_model(g_asset_model, 1); d3dview::focus_model(); }
            }
            return 0;
        }
        case WM_LBUTTONDOWN: { SetCapture(hwnd); return 0; }
        case WM_LBUTTONUP: { ReleaseCapture(); return 0; }
        case WM_MOUSEMOVE: {
            if (GetCapture() == hwnd) {
                static int lx = 0, ly = 0;
                int x = (short)LOWORD(lp), y = (short)HIWORD(lp);
                d3dview::mouse_drag(x - lx, y - ly);
                lx = x; ly = y;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        case WM_MOUSEWHEEL: {
            d3dview::mouse_wheel(GET_WHEEL_DELTA_WPARAM(wp));
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        case WM_KEYDOWN: {
            if (wp == 'F' || wp == 'f') d3dview::focus_model();
            if (wp == 'R' || wp == 'r') { d3dview::reset_camera(); d3dview::focus_model(); }
            if (wp == 'W' || wp == 'w') d3dview::load_model(g_asset_model, 0);
            if (wp == 'S' || wp == 's') d3dview::load_model(g_asset_model, 1);
            if (wp == 'B' || wp == 'b') d3dview::load_model(g_asset_model, 2);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        default: return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

static void layout_controls(HWND hWnd) {
    if (!hTabCtrl) return;
    RECT rc; GetClientRect(hWnd, &rc);
    const int tabHeight = 26;
    SetWindowPos(hTabCtrl, nullptr, 0, 0, rc.right, tabHeight, SWP_NOZORDER);
    const int mid = (rc.bottom - tabHeight) / 2 + tabHeight;
    if (currentTab == 6) { // ASSETS: list left, 3D viewport right, properties bottom
        int detH = 150;
        int listW = rc.right * 38 / 100;
        int vpX = listW, vpW = rc.right - listW;
        int bodyBottom = rc.bottom - detH;
        SetWindowPos(hListView, nullptr, 0, tabHeight, listW, bodyBottom - tabHeight, SWP_NOZORDER);
        SetWindowPos(hViewport, nullptr, vpX, tabHeight, vpW, bodyBottom - tabHeight, SWP_NOZORDER);
        SetWindowPos(hDetails, nullptr, 0, bodyBottom, rc.right, detH, SWP_NOZORDER);
    } else if (currentTab == 11) { // FUNCTIONS: search, list, details left + references right + Go bar
        int dW = rc.right * 62 / 100;          // details width
        int botTop = mid, botH = rc.bottom - mid;
        int goH = 26;
        int searchH = 24;
        int listTop = tabHeight + searchH;
        SetWindowPos(hSearchEdit, nullptr, 4, tabHeight + 2, rc.right - 8, searchH, SWP_NOZORDER);
        SetWindowPos(hListView, nullptr, 0, listTop, rc.right, mid - listTop, SWP_NOZORDER);
        SetWindowPos(hDetails, nullptr, 0, botTop, dW, botH - goH, SWP_NOZORDER);
        SetWindowPos(hRefView, nullptr, dW, botTop, rc.right - dW, botH - goH, SWP_NOZORDER);
        SetWindowPos(hGoEdit, nullptr, 0, rc.bottom - goH, dW - 80, goH, SWP_NOZORDER);
        SetWindowPos(hGoButton, nullptr, dW - 78, rc.bottom - goH, 78, goH, SWP_NOZORDER);
    } else {
        SetWindowPos(hListView, nullptr, 0, tabHeight, rc.right, mid - tabHeight, SWP_NOZORDER);
        SetWindowPos(hDetails, nullptr, 0, mid, rc.right, rc.bottom - mid, SWP_NOZORDER);
    }
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch(msg) {
        case WM_CREATE: {
            INITCOMMONCONTROLSEX icex{sizeof(icex), ICC_LISTVIEW_CLASSES|ICC_TAB_CLASSES};
            InitCommonControlsEx(&icex);
            RECT rc; GetClientRect(hWnd,&rc);
            const int tabHeight=26;
            hTabCtrl = CreateWindowExW(0,L"SysTabControl32",L"",WS_CHILD|WS_VISIBLE,0,0,rc.right,tabHeight,hWnd,(HMENU)1,GetModuleHandle(nullptr),nullptr);
            const wchar_t* tabs[]={L"OVERVIEW",L"CODE MAP",L"CALL GRAPH",L"EXECUTION",L"MEMORY",L"DYNAMIC CODE",L"ASSETS",L"GAME OBJECTS",L"ENGINE",L"PROGRESS",L"BLOCKERS",L"FUNCTIONS"};
            for(int i=0;i<12;i++){TCITEMW t{};t.mask=TCIF_TEXT;t.pszText=(LPWSTR)tabs[i];TabCtrl_InsertItem(hTabCtrl,i,&t);}
            hListView = CreateWindowExW(0,L"SysListView32",L"",WS_CHILD|WS_VISIBLE|WS_BORDER|LVS_REPORT|LVS_SINGLESEL,0,tabHeight,rc.right,rc.bottom-tabHeight,hWnd,(HMENU)2,GetModuleHandle(nullptr),nullptr);
            hDetails = CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_VSCROLL|ES_MULTILINE|ES_READONLY,0,tabHeight,rc.right,rc.bottom-tabHeight,hWnd,(HMENU)3,GetModuleHandle(nullptr),nullptr);
            hRefView = CreateWindowExW(0,L"SysListView32",L"",WS_CHILD|WS_BORDER|LVS_REPORT|LVS_SINGLESEL,
                0,0,0,0,hWnd,(HMENU)5,GetModuleHandle(nullptr),nullptr);
            hGoEdit = CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|ES_AUTOHSCROLL,
                0,0,0,0,hWnd,(HMENU)6,GetModuleHandle(nullptr),nullptr);
            hGoButton = CreateWindowExW(0,L"BUTTON",L"Go",WS_CHILD|BS_PUSHBUTTON,
                0,0,0,0,hWnd,(HMENU)7,GetModuleHandle(nullptr),nullptr);
            hSearchEdit = CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|ES_AUTOHSCROLL,
                0,0,0,0,hWnd,(HMENU)9,GetModuleHandle(nullptr),nullptr);
            // embedded 3D viewport (child window, drives D3D11 renderer)
            {
                WNDCLASSW vwc{};
                vwc.lpfnWndProc = ViewportProc;
                vwc.hInstance = GetModuleHandleW(nullptr);
                vwc.lpszClassName = L"DescompDSViewport";
                vwc.hCursor = LoadCursor(nullptr, IDC_ARROW);
                RegisterClassW(&vwc);
                hViewport = CreateWindowExW(0, L"DescompDSViewport", L"",
                    WS_CHILD | WS_VISIBLE | WS_BORDER, 0, tabHeight, 200, 200, hWnd,
                    (HMENU)8, GetModuleHandleW(nullptr), nullptr);
            }
            SendMessageW(hListView, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
            SendMessageW(hDetails, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
            SendMessageW(hRefView, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
            SendMessageW(hGoEdit, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
            SendMessageW(hGoButton, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
            SendMessageW(hSearchEdit, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
            build_addr_index();
            layout_controls(hWnd);
            populate_tab(0,g_data);
            break;
        }
        case WM_SIZE: {
            layout_controls(hWnd);
            break;
        }
        case WM_NOTIFY: {
            LPNMHDR hdr=(LPNMHDR)lParam;
            if(hdr->idFrom==1 && hdr->code==TCN_SELCHANGE){
                int sel=TabCtrl_GetCurSel(hTabCtrl);
                currentTab=sel;
                layout_controls(hWnd);
                populate_tab(sel,g_data);
            } else if(hdr->idFrom==2 && hdr->code==LVN_ITEMCHANGED){
                NMLISTVIEW* lv=(NMLISTVIEW*)lParam;
                if((lv->uNewState & LVIS_SELECTED) && !(lv->uOldState & LVIS_SELECTED)){
                    if(currentTab==0) {
                        show_overview_item(lv->iItem);
                    } else if(currentTab==2) {
                        show_edge_details(lv->iItem);
                    } else if(currentTab==3) {
                        show_trace_details(lv->iItem);
                    } else if(currentTab==4) {
                        show_memory_region(lv->iItem);
                    } else if(currentTab==5) {
                        show_dynamic_region(lv->iItem);
                    } else if(currentTab==8) {
                        show_engine_subsystem(lv->iItem);
                    } else if(currentTab==9) {
                        show_progress_metric(lv->iItem);
                    } else if(currentTab==10) {
                        show_blocker(lv->iItem);
                    } else if(currentTab==6) {
                        select_asset(lv->iItem);
                    } else if(currentTab==11) {
                        if (lv->iItem >= 0 && lv->iItem < (int)g_func_order.size()) {
                            int gi = g_func_order[lv->iItem];
                            show_function_details(gi);
                            g_sel_func = gi;
                        }
                    } else if(currentTab==1 && lv->iItem>=0 && lv->iItem<(int)g_codemap_order.size()){
                        auto& f=g_data.funcs[g_codemap_order[lv->iItem]];
                        std::wstringstream ss;
                        ss<<L"Address  : "<<s2ws(f.addr)<<L"\n";
                        ss<<L"Name     : "<<s2ws(f.name)<<L"\n";
                        ss<<L"Arch     : "<<(f.thumb?L"Thumb":L"ARM")<<L"\n";
                        ss<<L"Blocks   : "<<f.blocks<<L"\n";
                        ss<<L"Size     : "<<f.size<<L"\n";
                        ss<<L"Callees  : "<<f.callees<<L"\n";
                        ss<<L"Callers  : "<<f.callers<<L"\n";
                        ss<<L"Executed : "<<(f.executed?L"YES":L"NO")<<L"\n";
                        ss<<L"Dynamic  : "<<(f.dynamic?L"YES":L"NO")<<L"\n\n";
                        ss<<L"Double-click to open FUNCTIONS on this function.";
                        SetWindowTextW(hDetails, ss.str().c_str());
                    }
                }
            } else if(hdr->idFrom==2 && hdr->code==LVN_COLUMNCLICK){
                NMLISTVIEW* lv=(NMLISTVIEW*)lParam;
                int col = lv->iSubItem;
                if (currentTab==11) {
                    toggle_sort(g_sort_fn, col);
                    rebuild_func_list();
                    populate_functions(g_data);
                    if (g_sel_func>=0) for (int i=0;i<(int)g_func_order.size();i++)
                        if (g_func_order[i]==g_sel_func){ ListView_SetItemState(hListView,i,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED); ListView_EnsureVisible(hListView,i,FALSE); break; }
                } else if (currentTab==1) {
                    toggle_sort(g_sort_cm, col);
                    std::stable_sort(g_codemap_order.begin(), g_codemap_order.end(), [&](int a,int b)->bool{
                        const FuncInfo&A=g_data.funcs[a], &B=g_data.funcs[b]; int r=0;
                        switch(col){case 0: r=(parse_addr(A.addr)<parse_addr(B.addr))?-1:(parse_addr(A.addr)>parse_addr(B.addr)?1:0); break; case 1: r=A.name.compare(B.name); break; case 2: r=A.blocks-B.blocks; break; case 3: r=A.size-B.size; break; case 4: r=A.callees-B.callees; break; case 5: r=A.callers-B.callers; break; case 6: r=(int)A.executed-(int)B.executed; break; case 7: r=(int)A.dynamic-(int)B.dynamic; break; case 8: r=(int)A.thumb-(int)B.thumb; break; default: r=0;}
                        if(r==0) r=(parse_addr(A.addr)<parse_addr(B.addr))?-1:1;
                        return g_sort_cm.asc ? (r<0) : (r>0);
                    });
                    populate_code_map(g_data);
                } else if (currentTab==2) {
                    toggle_sort(g_sort_cg, col);
                    std::stable_sort(g_edges_order.begin(), g_edges_order.end(), [&](int a,int b)->bool{
                        const EdgeInfo&A=g_data.edges[a], &B=g_data.edges[b]; int r=0;
                        switch(col){case 0: r=(parse_addr(A.from)<parse_addr(B.from))?-1:(parse_addr(A.from)>parse_addr(B.from)?1:0); break; case 1: r=(parse_addr(A.to)<parse_addr(B.to))?-1:(parse_addr(A.to)>parse_addr(B.to)?1:0); break; case 2: r=A.type.compare(B.type); break; case 3: r=(int)A.executed-(int)B.executed; break; default: r=0;}
                        if(r==0) r=(parse_addr(A.from)<parse_addr(B.from))?-1:1;
                        return g_sort_cg.asc ? (r<0) : (r>0);
                    });
                    populate_call_graph(g_data);
                } else if (currentTab==3) {
                    toggle_sort(g_sort_ex, col);
                    std::stable_sort(g_trace_order.begin(), g_trace_order.end(), [&](int a,int b)->bool{
                        const TraceEntry&A=g_data.trace[a], &B=g_data.trace[b]; int r=0;
                        switch(col){case 0: r=A.idx-B.idx; break; case 1: r=(parse_addr(A.pc)<parse_addr(B.pc))?-1:(parse_addr(A.pc)>parse_addr(B.pc)?1:0); break; case 2: r=A.mnemonic.compare(B.mnemonic); break; case 3: r=A.func.compare(B.func); break; case 4: r=A.block.compare(B.block); break; case 5: r=(int)A.thumb-(int)B.thumb; break; default: r=0;}
                        if(r==0) r=A.idx-B.idx;
                        return g_sort_ex.asc ? (r<0) : (r>0);
                    });
                    populate_execution(g_data);
                }
            } else if(hdr->idFrom==2 && hdr->code==NM_DBLCLK){
                NMITEMACTIVATE* nia = (NMITEMACTIVATE*)lParam;
                int row = nia->iItem, sub = nia->iSubItem;
                if (currentTab==0) {
                    if (row>=0 && row<(int)g_ov_target.size() && g_ov_target[row]>=0)
                        goto_tab(g_ov_target[row]);
                } else if (currentTab==1) {
                    if (row>=0 && row<(int)g_codemap_order.size())
                        goto_function(g_codemap_order[row]);
                } else if (currentTab==2) {
                    if (row>=0 && row<(int)g_edges_order.size()) {
                        const EdgeInfo& e = g_data.edges[g_edges_order[row]];
                        goto_address(sub==1 ? e.to : e.from);
                    }
                } else if (currentTab==3) {
                    if (row>=0 && row<(int)g_trace_order.size())
                        goto_address(g_data.trace[g_trace_order[row]].func);
                }
            } else if(hdr->idFrom==5 && hdr->code==LVN_ITEMCHANGED){
                NMLISTVIEW* lv=(NMLISTVIEW*)lParam;
                if(currentTab==11 && (lv->uNewState & LVIS_SELECTED) && !(lv->uOldState & LVIS_SELECTED)){
                    // navigate: read Address (column 1) of selected reference row
                    wchar_t buf[64];
                    ListView_GetItemText(hRefView, lv->iItem, 1, buf, 64);
                    navigate_to_address(ws2s(buf));
                }
            }
            break;
        }
        case WM_COMMAND: {
            if (LOWORD(wParam) == 7 && HIWORD(wParam) == BN_CLICKED) {
                wchar_t buf[64];
                GetWindowTextW(hGoEdit, buf, 64);
                navigate_to_address(ws2s(buf));
            } else if (LOWORD(wParam) == 9 && HIWORD(wParam) == EN_CHANGE) {
                wchar_t buf[128];
                GetWindowTextW(hSearchEdit, buf, 128);
                int prev = g_sel_func;
                g_func_search = buf;
                populate_functions(g_data);
                // preserve selection if still visible after filter
                if (prev >= 0) {
                    for (int i = 0; i < (int)g_func_order.size(); i++) {
                        if (g_func_order[i] == prev) {
                            ListView_SetItemState(hListView, i, LVIS_SELECTED|LVIS_FOCUSED, LVIS_SELECTED|LVIS_FOCUSED);
                            ListView_EnsureVisible(hListView, i, FALSE);
                            break;
                        }
                    }
                }
            }
            break;
        }
        case WM_DESTROY:
            if (d3dview::initialized()) d3dview::shutdown();
            PostQuitMessage(0);
            break;
        default: return DefWindowProcW(hWnd,msg,wParam,lParam);
    }
    return 0;
}

int WINAPI wWinMain(HINSTANCE hInst,HINSTANCE hPrev, LPWSTR lpCmdLine, int nCmdShow){
    std::string dir;
    // Parse first command-line argument as data directory
    int wargc=0;
    LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
    if(wargv && wargc>1 && wargv[1] && wargv[1][0]!='\0'){
        dir = ws2s(wargv[1]);
    }
    if(wargv) LocalFree(wargv);

    if(dir.empty()){
        // Fallback: search for output_mario walking up from exe directory
        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        std::wstring exeDir = exePath;
        size_t pos = exeDir.find_last_of(L"\\/");
        if(pos!=std::wstring::npos) exeDir = exeDir.substr(0,pos);
        std::wstring cur = exeDir;
        for(int i=0;i<8;i++){
            std::wstring test = cur + L"\\output_mario";
            DWORD attr = GetFileAttributesW(test.c_str());
            if(attr!=INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)){
                dir = ws2s(test);
                break;
            }
            size_t p2 = cur.find_last_of(L"\\/");
            if(p2==std::wstring::npos) break;
            cur = cur.substr(0,p2);
        }
    }
    if(dir.empty()) dir = "D:/DescompDS/output_mario";

    // Resolve ROM path: try roms/Mario.nds relative to data dir, else exe-relative search.
    {
        std::string cand = dir + "/../roms/Mario.nds";
        DWORD attr = GetFileAttributesW(s2ws(cand).c_str());
        if (attr == INVALID_FILE_ATTRIBUTES) cand = "D:/DescompDS/roms/Mario.nds";
        g_rom_path = cand;
    }

    std::string diag;
    load_data(dir, g_data, diag);

    WNDCLASSW wc{};
    wc.lpfnWndProc=WndProc;
    wc.hInstance=hInst;
    wc.lpszClassName=CLASS_NAME;
    wc.hCursor=LoadCursor(nullptr,IDC_ARROW);
    wc.hbrBackground=(HBRUSH)(COLOR_BTNFACE+1);
    RegisterClassW(&wc);
    HWND hWnd=CreateWindowExW(0,CLASS_NAME,L"DescompDS Explorer — Visual Map",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,1100,700,nullptr,nullptr,hInst,nullptr);
    hMainWnd=hWnd;
    ShowWindow(hWnd,nCmdShow);
    UpdateWindow(hWnd);
    MSG msg;
    while(GetMessageW(&msg,nullptr,0,0)){
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}