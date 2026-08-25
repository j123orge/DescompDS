#include "runtime/nds_runtime.h"
#include "runtime/subsystem_access.h"
#include "runtime/dynamic_code.h"
#include <vector>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <unordered_set>

namespace descomp::runtime {

// --- Backing stores ---
static std::vector<uint8_t> g_main_ram;   // 4 MB
static std::vector<uint8_t> g_itcm;       // 32K
static std::vector<uint8_t> g_dtcm;       // 16K
static std::vector<uint8_t> g_io;         // 64K stub
static std::vector<uint8_t> g_bios9;      // 4K
static std::vector<uint8_t> g_bios7;      // 16K
static std::vector<uint8_t> g_rom;        // cart mirror (optional)
static std::vector<uint8_t> g_shared_wram; // 32K

static uint32_t g_main_base = 0x02000000;
static constexpr uint32_t kMainSize = 4*1024*1024;
static constexpr uint32_t kItcmBase = 0x00000000;
static constexpr uint32_t kItcmSize = 32*1024;
static constexpr uint32_t kDtcmBase = 0x027E0000;
static constexpr uint32_t kDtcmSize = 16*1024;
static constexpr uint32_t kIoBase = 0x04000000;
static constexpr uint32_t kIoSize = 64*1024;
static constexpr uint32_t kBios9Base = 0xFFFF0000;
static constexpr uint32_t kBios9Size = 4*1024;
static constexpr uint32_t kBios7Base = 0x00000000;
static constexpr uint32_t kBios7Size = 16*1024;
static constexpr uint32_t kRomBase = 0x08000000;
static constexpr uint32_t kSharedWramBase = 0x03000000;
static constexpr uint32_t kSharedWramSize = 32*1024;

static Cp15State g_cp15;
static BusStats g_stats;

// Provenance: per 4K page for Main RAM
static constexpr uint32_t kPageSize = 4096;
static std::vector<uint32_t> g_main_generation; // per 4K page
static std::vector<uint8_t> g_main_written;     // per byte written flag
static uint32_t g_generation_counter = 1;

static bool g_has_fault = false;
static uint32_t g_fault_addr = 0;
static uint32_t g_fault_raw = 0;
static void (*g_handler)(uint32_t,uint32_t) = nullptr;

// Track unknown regions to avoid log spam (log first N)
static std::unordered_set<uint32_t> g_logged_unmapped;
static constexpr size_t kMaxUnmappedLogs = 64;

Cp15State& cp15_state() { return g_cp15; }
BusStats& bus_stats() { return g_stats; }
void clear_bus_stats() { g_stats = BusStats{}; g_logged_unmapped.clear(); }

Region resolve_region(uint32_t addr) {
    // Priority: ITCM > DTCM > Main > Shared > IO > BIOS > ROM > Unmapped
    // ITCM at 0x00000000 32K, but BIOS7 also there — ITCM takes priority when enabled
    if (g_cp15.itcm_enable && addr >= kItcmBase && addr < kItcmBase + kItcmSize) return Region::Itcm;
    if (g_cp15.dtcm_enable && addr >= g_cp15.dtcm_base && addr < g_cp15.dtcm_base + g_cp15.dtcm_size) return Region::Dtcm;
    // Also check default DTCM base (for early boot before CP15 configured)
    if (addr >= kDtcmBase && addr < kDtcmBase + kDtcmSize) return Region::Dtcm;
    if (addr >= g_main_base && addr < g_main_base + kMainSize) return Region::MainRam;
    if (addr >= kSharedWramBase && addr < kSharedWramBase + kSharedWramSize) return Region::SharedWram;
    if (addr >= kIoBase && addr < kIoBase + kIoSize) return Region::Io;
    if (addr >= kBios9Base && addr < kBios9Base + kBios9Size) return Region::Bios9;
    // BIOS7 overlaps ITCM, already handled above. If ITCM disabled, check BIOS7
    if (addr < kBios7Size) {
        if (!g_cp15.itcm_enable || addr >= kItcmSize) return Region::Bios7;
    }
    if (addr >= kRomBase && addr < kRomBase + 32*1024*1024) return Region::Rom; // up to 32M
    if (addr >= 0x05000000 && addr < 0x07000000) return Region::Unmapped; // palette/vram etc -> unmapped for now
    return Region::Unmapped;
}

const char* region_name(Region r) {
    switch(r){
        case Region::MainRam: return "MainRam";
        case Region::Itcm: return "ITCM";
        case Region::Dtcm: return "DTCM";
        case Region::Io: return "IO";
        case Region::Bios9: return "BIOS9";
        case Region::Bios7: return "BIOS7";
        case Region::Rom: return "ROM";
        case Region::SharedWram: return "SharedWRAM";
        case Region::Unmapped: return "Unmapped";
    }
    return "Unknown";
}
bool is_io_region(uint32_t addr){ return resolve_region(addr)==Region::Io; }
bool is_bios_region(uint32_t addr){ auto r=resolve_region(addr); return r==Region::Bios9||r==Region::Bios7; }

bool has_write_provenance(uint32_t addr, uint32_t size){
    // Only MainRam tracked for Phase 3.1
    if (resolve_region(addr)!=Region::MainRam) return false;
    uint32_t off = addr - g_main_base;
    for(uint32_t i=0;i<size;++i){
        if(off+i < g_main_written.size() && g_main_written[off+i]) return true;
    }
    return false;
}
void note_write_provenance(uint32_t addr, uint32_t size){
    if (resolve_region(addr)!=Region::MainRam) return;
    uint32_t off = addr - g_main_base;
    for(uint32_t i=0;i<size;++i){
        if(off+i < g_main_written.size()) g_main_written[off+i]=1;
    }
    // bump generation per 4K page
    uint32_t page = off / kPageSize;
    if(page < g_main_generation.size()){
        g_main_generation[page] = g_generation_counter++;
        if(g_generation_counter==0) g_generation_counter=1;
    }
}

static void log_unmapped(bool is_read, uint32_t addr, uint32_t size, uint32_t val){
    uint32_t key = addr & ~0x3u;
    if(g_logged_unmapped.size() < kMaxUnmappedLogs && g_logged_unmapped.find(key)==g_logged_unmapped.end()){
        g_logged_unmapped.insert(key);
        std::fprintf(stderr, "[bus] %s %s 0x%08X size=%u val=0x%08X\n",
            is_read?"read":"write", region_name(resolve_region(addr)), addr, size, val);
    }
}

void init_memory(const uint8_t* arm9_data, size_t arm9_size, uint32_t arm9_base) {
    init_memory_ex(arm9_data, arm9_size, arm9_base, nullptr,0,nullptr,0,nullptr,0);
}
void init_memory_ex(const uint8_t* arm9_data, size_t arm9_size, uint32_t arm9_base,
                    const uint8_t* bios9_data, size_t bios9_size,
                    const uint8_t* bios7_data, size_t bios7_size,
                    const uint8_t* rom_data, size_t rom_size) {
    g_main_base = arm9_base;
    g_main_ram.assign(kMainSize, 0);
    if(arm9_data && arm9_size){
        size_t copy = std::min<size_t>(arm9_size, kMainSize);
        std::memcpy(g_main_ram.data(), arm9_data, copy);
        // Also copy to offset if base != 0x02000000? base is usually 0x02000000
        // For simplicity, place at offset 0 (already)
    }
    g_itcm.assign(kItcmSize, 0);
    g_dtcm.assign(kDtcmSize, 0);
    g_io.assign(kIoSize, 0);
    g_bios9.assign(kBios9Size, 0);
    if(bios9_data && bios9_size) std::memcpy(g_bios9.data(), bios9_data, std::min<size_t>(bios9_size, kBios9Size));
    g_bios7.assign(kBios7Size, 0);
    if(bios7_data && bios7_size) std::memcpy(g_bios7.data(), bios7_data, std::min<size_t>(bios7_size, kBios7Size));
    g_shared_wram.assign(kSharedWramSize, 0);
    if(rom_data && rom_size){
        g_rom.assign(rom_size, 0);
        std::memcpy(g_rom.data(), rom_data, rom_size);
    } else {
        g_rom.clear();
    }
    g_main_generation.assign(kMainSize / kPageSize, 0);
    g_main_written.assign(kMainSize, 0);
    g_generation_counter = 1;
    g_has_fault = false;
    clear_bus_stats();
    // reset CP15 to defaults
    g_cp15 = Cp15State{};
}

void shutdown_memory(){
    g_main_ram.clear(); g_itcm.clear(); g_dtcm.clear(); g_io.clear();
    g_bios9.clear(); g_bios7.clear(); g_shared_wram.clear();
    g_main_generation.clear(); g_main_written.clear();
}

uint8_t read8(uint32_t addr){
    // Track subsystem accesses
    track_io_access_integration(addr, true, 1);
    track_vram_access_integration(addr, true, 1);
    track_gpu_2d_access_integration(addr, true, 1);
    track_gpu_3d_access_integration(addr, true, 1);
    track_audio_access_integration(addr, true, 1);
    track_cartridge_access_integration(addr, true, 1);
    track_timer_access_integration(addr, true, 0);
    track_ipc_access_integration(addr, true, 0);

    Region r = resolve_region(addr);
    switch(r){
        case Region::MainRam: {
            uint32_t off = addr - g_main_base;
            if(off < g_main_ram.size()){ g_stats.ram_reads++; return g_main_ram[off];}
            break;
        }
        case Region::Itcm: {
            uint32_t off = addr - kItcmBase;
            if(off < g_itcm.size()){ g_stats.itcm_reads++; return g_itcm[off];}
            break;
        }
        case Region::Dtcm: {
            uint32_t base = g_cp15.dtcm_enable ? g_cp15.dtcm_base : kDtcmBase;
            uint32_t off = addr - base;
            if(off < g_dtcm.size()){ g_stats.dtcm_reads++; return g_dtcm[off];}
            break;
        }
        case Region::SharedWram: {
            uint32_t off = addr - kSharedWramBase;
            if(off < g_shared_wram.size()){ g_stats.ram_reads++; return g_shared_wram[off];}
            break;
        }
        case Region::Io: {
            uint32_t off = addr - kIoBase;
            if(off < g_io.size()){ g_stats.io_reads++; trace_io_access(true,addr,1,g_io[off]); return g_io[off];}
            break;
        }
        case Region::Bios9: {
            uint32_t off = addr - kBios9Base;
            if(off < g_bios9.size()){ g_stats.bios_reads++; return g_bios9[off];}
            break;
        }
        case Region::Bios7: {
            uint32_t off = addr - kBios7Base;
            if(off < g_bios7.size()){ g_stats.bios_reads++; return g_bios7[off];}
            break;
        }
        case Region::Rom: {
            uint32_t off = addr - kRomBase;
            if(off < g_rom.size()){ g_stats.rom_reads++; return g_rom[off];}
            // ROM mirror: if no ROM data, return deterministic 0xFF (open bus)
            g_stats.rom_reads++; return 0xFF;
        }
        default: break;
    }
    // Unmapped
    g_stats.unmapped_reads++;
    log_unmapped(true, addr,1,0);
    return 0; // deterministic
}
uint16_t read16(uint32_t addr){
    // Track subsystem accesses
    track_io_access_integration(addr, true, 2);
    track_vram_access_integration(addr, true, 2);
    track_gpu_2d_access_integration(addr, true, 2);
    track_gpu_3d_access_integration(addr, true, 2);
    track_audio_access_integration(addr, true, 2);
    track_cartridge_access_integration(addr, true, 2);
    track_timer_access_integration(addr, true, 0);
    track_ipc_access_integration(addr, true, 0);

    // Handle unaligned? NDS allows. Do byte-wise for unmapped safety
    if((addr & 1)==0){
        Region r = resolve_region(addr);
        if(r==Region::MainRam || r==Region::Itcm || r==Region::Dtcm || r==Region::SharedWram || r==Region::Bios9 || r==Region::Bios7){
            uint8_t lo = read8(addr);
            uint8_t hi = read8(addr+1);
            return static_cast<uint16_t>(lo | (hi<<8));
        }
    }
    // Fallback byte-wise
    uint8_t lo = read8(addr);
    uint8_t hi = read8(addr+1);
    return static_cast<uint16_t>(lo | (hi<<8));
}
uint32_t read32(uint32_t addr){
    // Track subsystem accesses
    track_io_access_integration(addr, true, 4);
    track_vram_access_integration(addr, true, 4);
    track_gpu_2d_access_integration(addr, true, 4);
    track_gpu_3d_access_integration(addr, true, 4);
    track_audio_access_integration(addr, true, 4);
    track_cartridge_access_integration(addr, true, 4);
    track_timer_access_integration(addr, true, 0);
    track_ipc_access_integration(addr, true, 0);

    if((addr & 3)==0){
        Region r = resolve_region(addr);
        if(r==Region::MainRam || r==Region::Itcm || r==Region::Dtcm || r==Region::SharedWram || r==Region::Bios9 || r==Region::Bios7){
            uint32_t v = 0;
            std::memcpy(&v, 
                r==Region::MainRam ? &g_main_ram[addr - g_main_base] :
                r==Region::Itcm ? &g_itcm[addr - kItcmBase] :
                r==Region::Dtcm ? &g_dtcm[addr - (g_cp15.dtcm_enable?g_cp15.dtcm_base:kDtcmBase)] :
                r==Region::SharedWram ? &g_shared_wram[addr - kSharedWramBase] :
                r==Region::Bios9 ? &g_bios9[addr - kBios9Base] : &g_bios7[addr - kBios7Base], 4);
            // stats already counted in byte path? Count here for 32
            // We counted per byte above, avoid double. Just increment appropriately
            // Instead, adjust: we already counted in read8 path if we used it. For fast path count separate.
            // For simplicity, count here
            if(r==Region::MainRam) g_stats.ram_reads++;
            else if(r==Region::Itcm) g_stats.itcm_reads++;
            else if(r==Region::Dtcm) g_stats.dtcm_reads++;
            else if(r==Region::SharedWram) g_stats.ram_reads++;
            else if(r==Region::Bios9||r==Region::Bios7) g_stats.bios_reads++;
            return v;
        }
    }
    // fallback byte-wise (Handles IO/ROM/unmapped with logging)
    uint32_t v = read8(addr) | (read8(addr+1)<<8) | (read8(addr+2)<<16) | (read8(addr+3)<<24);
    return v;
}
void write8(uint32_t addr, uint8_t val){
    // Track subsystem accesses
    track_io_access_integration(addr, false, 1);
    track_vram_access_integration(addr, false, 1);
    track_gpu_2d_access_integration(addr, false, 1);
    track_gpu_3d_access_integration(addr, false, 1);
    track_audio_access_integration(addr, false, 1);
    track_cartridge_access_integration(addr, false, 1);
    track_timer_access_integration(addr, false, val);
    track_ipc_access_integration(addr, false, val);

    if((addr>=kItcmBase && addr < kItcmBase+kItcmSize) || (addr>= g_cp15.dtcm_base && addr < g_cp15.dtcm_base+g_cp15.dtcm_size)) DynamicCodeManager::instance().note_write(addr,1);
    Region r = resolve_region(addr);
    switch(r){
        case Region::MainRam: {
            uint32_t off = addr - g_main_base;
            if(off < g_main_ram.size()){ g_main_ram[off]=val; g_stats.ram_writes++; note_write_provenance(addr,1); return;}
            break;
        }
        case Region::Itcm: {
            uint32_t off = addr - kItcmBase;
            if(off < g_itcm.size()){ g_itcm[off]=val; g_stats.itcm_writes++; return;}
            break;
        }
        case Region::Dtcm: {
            uint32_t base = g_cp15.dtcm_enable ? g_cp15.dtcm_base : kDtcmBase;
            uint32_t off = addr - base;
            if(off < g_dtcm.size()){ g_dtcm[off]=val; g_stats.dtcm_writes++; return;}
            break;
        }
        case Region::SharedWram: {
            uint32_t off = addr - kSharedWramBase;
            if(off < g_shared_wram.size()){ g_shared_wram[off]=val; g_stats.ram_writes++; return;}
            break;
        }
        case Region::Io: {
            uint32_t off = addr - kIoBase;
            if(off < g_io.size()){ g_io[off]=val; g_stats.io_writes++; trace_io_access(false,addr,1,val); return;}
            break;
        }
        case Region::Bios9:
        case Region::Bios7:
        case Region::Rom: {
            // read-only stub: log but ignore
            g_stats.unmapped_writes++;
            log_unmapped(false, addr,1,val);
            return;
        }
        default: break;
    }
    g_stats.unmapped_writes++;
    log_unmapped(false, addr,1,val);
}
void write16(uint32_t addr, uint16_t val){
    // Track subsystem accesses
    track_io_access_integration(addr, false, 2);
    track_vram_access_integration(addr, false, 2);
    track_gpu_2d_access_integration(addr, false, 2);
    track_gpu_3d_access_integration(addr, false, 2);
    track_audio_access_integration(addr, false, 2);
    track_cartridge_access_integration(addr, false, 2);
    track_timer_access_integration(addr, false, val);
    track_ipc_access_integration(addr, false, val);

    if((addr>=kItcmBase && addr < kItcmBase+kItcmSize) || (addr>= g_cp15.dtcm_base && addr < g_cp15.dtcm_base+g_cp15.dtcm_size)) DynamicCodeManager::instance().note_write(addr,2);
    write8(addr, val & 0xFF);
    write8(addr+1, (val>>8)&0xFF);
}
void write32(uint32_t addr, uint32_t val){
    // Track subsystem accesses
    track_io_access_integration(addr, false, 4);
    track_vram_access_integration(addr, false, 4);
    track_gpu_2d_access_integration(addr, false, 4);
    track_gpu_3d_access_integration(addr, false, 4);
    track_audio_access_integration(addr, false, 4);
    track_cartridge_access_integration(addr, false, 4);
    track_timer_access_integration(addr, false, val);
    track_ipc_access_integration(addr, false, val);

    if((addr>=kItcmBase && addr < kItcmBase+kItcmSize) || (addr>= g_cp15.dtcm_base && addr < g_cp15.dtcm_base+g_cp15.dtcm_size)) DynamicCodeManager::instance().note_write(addr,4);
    // Fast path for aligned RAM
    if((addr & 3)==0){
        Region r = resolve_region(addr);
        if(r==Region::MainRam && addr - g_main_base +4 <= g_main_ram.size()){
            std::memcpy(&g_main_ram[addr - g_main_base], &val, 4);
            g_stats.ram_writes++; note_write_provenance(addr,4); return;
        }
        if(r==Region::Itcm && addr - kItcmBase +4 <= g_itcm.size()){
            std::memcpy(&g_itcm[addr - kItcmBase], &val, 4);
            g_stats.itcm_writes++; return;
        }
        if(r==Region::Dtcm){
            uint32_t base = g_cp15.dtcm_enable ? g_cp15.dtcm_base : kDtcmBase;
            if(addr >= base && addr - base +4 <= g_dtcm.size()){
                std::memcpy(&g_dtcm[addr - base], &val, 4);
                g_stats.dtcm_writes++; return;
            }
        }
        if(r==Region::SharedWram && addr - kSharedWramBase +4 <= g_shared_wram.size()){
            std::memcpy(&g_shared_wram[addr - kSharedWramBase], &val, 4);
            g_stats.ram_writes++; return;
        }
        if(r==Region::Io && addr - kIoBase +4 <= g_io.size()){
            std::memcpy(&g_io[addr - kIoBase], &val, 4);
            g_stats.io_writes++; trace_io_access(false,addr,4,val); return;
        }
        if(r==Region::Bios9 || r==Region::Bios7 || r==Region::Rom){
            g_stats.unmapped_writes++;
            log_unmapped(false, addr,4,val);
            return;
        }
    }
    write8(addr, val & 0xFF);
    write8(addr+1, (val>>8)&0xFF);
    write8(addr+2, (val>>16)&0xFF);
    write8(addr+3, (val>>24)&0xFF);
}

extern "C" {
 uint32_t nds_hal_read32(uint32_t a){ return read32(a); }
 uint16_t nds_hal_read16(uint32_t a){ return read16(a); }
 uint8_t  nds_hal_read8(uint32_t a){ return read8(a); }
 void nds_hal_write32(uint32_t a, uint32_t v){ write32(a,v); }
 void nds_hal_write16(uint32_t a, uint16_t v){ write16(a,v); }
 void nds_hal_write8(uint32_t a, uint8_t v){ write8(a,v); }
 void nds_unimplemented(uint32_t a, uint32_t r){
     g_has_fault = true; g_fault_addr = a; g_fault_raw = r;
     if(g_handler) g_handler(a,r);
     else std::fprintf(stderr, "[nds_unimplemented] %08X raw %08X\n", a, r);
 }
 void nds_swi(uint32_t id){
     g_stats.swi_count++;
     trace_swi(id, 0);
     // Minimal boot SWIs: handle immediately here, for others just log
     // Known boot SWIs for Mario: 0x00 SoftReset, 0x05 VBlankIntrWait, 0x0B CpuSet, 0x06 Div, 0x0C CpuFastSet, 0x0E BgAffineSet, etc.
     // For Phase 3.1 we just count and return; actual handlers are no-ops that set flags or copy memory
     // CpuSet (0x0B) and CpuFastSet (0x0C) should be emulated as memcpy with correct size, but for now we log
     if(id==0x05){
         // VBlankIntrWait: in real HW would wait for VBlank IRQ. For interpreter, just treat as nop that may set IRQ flag
         // Do not fault, just continue
     } else if(id==0x0B || id==0x0C){
         // CpuSet/CpuFastSet: would copy memory via DMA-like operation. For now, log and continue
         // Real implementation would need R0=src, R1=dst, R2=len|mode. Interpreter can't easily access regs here,
         // so we rely on caller to have set up memory; for Phase 3.1 we treat as nop
     }
     // For unknown SWI, increment unknown counter but still continue
     if(id!=0x00 && id!=0x05 && id!=0x06 && id!=0x07 && id!=0x0B && id!=0x0C && id!=0x0E && id!=0x0F && id!=0x10 && id!=0x11){
         g_stats.unknown_swi++;
         std::fprintf(stderr, "[swi] unknown SWI 0x%02X\n", (unsigned)id);
     }
 }
 void nds_bkpt(uint32_t id){ (void)id; std::fprintf(stderr, "[bkpt] 0x%X\n", (unsigned)id); }

 static uint32_t tcm_bytes(uint32_t reg){
     // size field bits 1-5: 512 << N where N = (reg>>1 &0x1F)
     uint32_t n = (reg>>1) & 0x1F;
     if(n<3) return 0;
     return 512u << n;
 }

 uint32_t nds_mrc(uint32_t cp, uint32_t op2, uint32_t crn, uint32_t crm){
     g_stats.cp15_reads++;
     if(cp != 15){
         g_stats.unknown_cp15++;
         std::fprintf(stderr, "[cp15] MRC p%u c%u c%u op2=%u -> 0 (unsupported cp)\n", (unsigned)cp,(unsigned)crn,(unsigned)crm,(unsigned)op2);
         return 0;
     }
     // ARM946E-S CP15 registers
     if(crn==0 && crm==0){
         if(op2==0) return 0x41059461; // Main ID
         if(op2==1) return 0x0F0D2112; // Cache type (approx)
         if(op2==2) return 0x00000000; // TCM type
     }
     if(crn==1 && crm==0 && op2==0){
         // Control register
         return g_cp15.control;
     }
     if(crn==1 && crm==0 && op2==1){
         // Aux control? return 0
         return 0;
     }
     if(crn==2 && crm==0){
         // Cachability
         if(op2==0) return 0; // D cachable
         if(op2==1) return 0; // I cachable
     }
     if(crn==3 && crm==0 && op2==0){
         // Bufferability
         return 0;
     }
     if(crn==5 && crm==0){
         // Access permission
         return 0;
     }
     if(crn==6){
         // MPU region
         uint32_t idx = crm; // actually crm encodes region? Simplified
         if(idx < 8 && g_cp15.mpu[idx].enable) return g_cp15.mpu[idx].base | 1;
         return 0;
     }
     if(crn==9){
         if(crm==1 && op2==0){
             // DTCM base/size
             return g_cp15.dtcm_base | (g_cp15.dtcm_enable?1:0);
         }
         if(crm==1 && op2==1){
             // ITCM base/size
             return g_cp15.itcm_base | (g_cp15.itcm_enable?1:0);
         }
         if(crm==0){
             // Cache lockdown
             return 0;
         }
     }
     if(crn==7){
         // Cache ops: return 0
         return 0;
     }
     if(crn==10){
         // TLB lockdown
         return 0;
     }
     g_stats.unknown_cp15++;
     trace_cp15_access(true, cp, op2, crn, crm, 0);
     std::fprintf(stderr, "[cp15] MRC p15 c%u c%u op2=%u -> 0 (unknown)\n", (unsigned)crn,(unsigned)crm,(unsigned)op2);
     return 0;
 }
 void nds_mcr(uint32_t cp, uint32_t op2, uint32_t val, uint32_t crn, uint32_t crm){
     g_stats.cp15_writes++;
     if(cp != 15){
         g_stats.unknown_cp15++;
         std::fprintf(stderr, "[cp15] MCR p%u c%u c%u op2=%u val=0x%08X (unsupported cp)\n", (unsigned)cp,(unsigned)crn,(unsigned)crm,(unsigned)op2,(unsigned)val);
         return;
     }
     trace_cp15_access(false, cp, op2, crn, crm, val);
     if(crn==1 && crm==0 && op2==0){
         g_cp15.control = val;
         // bit 16 ITCM enable, bit 18 ITCM? Actually NDS uses bits 16 DTCM, 18 ITCM, 12 I-cache, 2 D-cache, 0 MPU
         g_cp15.itcm_enable = (val & (1u<<18)) != 0;
         g_cp15.dtcm_enable = (val & (1u<<16)) != 0;
         g_cp15.mpu_enable = (val & 1) != 0;
         return;
     }
     if(crn==9 && crm==1){
         if(op2==0){
             g_cp15.dtcm_base = val & ~0xFFFu;
             g_cp15.dtcm_enable = (val & 1) != 0;
             // size handling simplified
             return;
         }
         if(op2==1){
             g_cp15.itcm_base = 0; // ITCM always 0 on NDS
             g_cp15.itcm_enable = (val & 1) != 0;
             return;
         }
     }
     if(crn==6){
         // MPU region: crm is region number? Actually encoding: MCR p15,0,Rd,c6,cX,0 where X*2 selects region
         // Simplified: use crm as index if <8
         if(crm < 8){
             g_cp15.mpu[crm].base = val & ~0xFFFu;
             g_cp15.mpu[crm].enable = (val & 1) != 0;
             // size derived from N field? For Phase 3.1, just store base
             // size = 1 << ((val>>1 &0x1F)+1) but we use tcm_bytes helper
             uint32_t sz = tcm_bytes(val);
             if(sz) g_cp15.mpu[crm].size = sz;
             return;
         }
     }
     if(crn==7){
         // Cache ops: ICache invalidate, DCache flush, etc. Nop for Phase 3.1
         return;
     }
     if(crn==2 || crn==3 || crn==5){
         // Cachability/bufferability/permissions: accept
         return;
     }
     if(crn==9 && crm==0){
         // lockdown: accept
         return;
     }
     if(crn==10){
         // TLB: accept
         return;
     }
     g_stats.unknown_cp15++;
     std::fprintf(stderr, "[cp15] MCR p15 c%u c%u op2=%u val=0x%08X (unknown)\n", (unsigned)crn,(unsigned)crm,(unsigned)op2,(unsigned)val);
 }
}

void set_unimplemented_handler(void (*h)(uint32_t,uint32_t)){ g_handler = h; }
bool has_unimplemented_fault(){ return g_has_fault; }
uint32_t fault_address(){ return g_fault_addr; }
uint32_t fault_raw(){ return g_fault_raw; }
void clear_fault(){ g_has_fault=false; }

void trace_cp15_access(bool is_read, uint32_t cp, uint32_t op2, uint32_t crn, uint32_t crm, uint32_t val){
    (void)is_read;(void)cp;(void)op2;(void)crn;(void)crm;(void)val;
}
void trace_io_access(bool is_read, uint32_t addr, uint32_t size, uint32_t val){
    (void)is_read;(void)addr;(void)size;(void)val;
}
void trace_swi(uint32_t id, uint32_t pc){
    (void)id;(void)pc;
}
const uint8_t* get_itcm_data(){ return g_itcm.data(); }
size_t get_itcm_size(){ return g_itcm.size(); }
const uint8_t* get_dtcm_data(){ return g_dtcm.data(); }
size_t get_dtcm_size(){ return g_dtcm.size(); }
const uint8_t* get_main_data(){ return g_main_ram.data(); }
size_t get_main_size(){ return g_main_ram.size(); }

} // namespace descomp::runtime
