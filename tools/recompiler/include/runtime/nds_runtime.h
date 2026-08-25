#pragma once

#include <cstdint>
#include <array>
#include <string>
#include <vector>

namespace descomp::runtime {

// ARM9 CPU state — preserves R0-R15, CPSR, SPSR and Thumb mode.
// CPSR layout (ARMv5TE): N[31] Z[30] C[29] V[28] | ... | T[5] | mode[4:0]
struct CPUState {
    uint32_t r[16] = {};   // R0-R12, SP(R13), LR(R14), PC(R15)
    uint32_t cpsr = 0x13;  // Supervisor mode initial
    uint32_t spsr = 0;
    bool thumb = false;

    // Convenience helpers
    inline bool flag_n() const { return (cpsr >> 31) & 1; }
    inline bool flag_z() const { return (cpsr >> 30) & 1; }
    inline bool flag_c() const { return (cpsr >> 29) & 1; }
    inline bool flag_v() const { return (cpsr >> 28) & 1; }
    inline void set_flags(bool n, bool z, bool c, bool v) {
        cpsr = (cpsr & 0x0FFFFFFF) | (n<<31) | (z<<30) | (c<<29) | (v<<28);
    }
    inline void set_thumb(bool t) {
        thumb = t;
        if (t) cpsr |= (1u << 5);
        else cpsr &= ~(1u << 5);
    }
    inline bool thumb_flag() const { return (cpsr >> 5) & 1; }
    inline void sync_thumb_from_cpsr() { thumb = thumb_flag(); }
};

// Memory regions (GBATEK-inspired minimal map)
enum class Region {
    MainRam,    // 0x02000000 4M
    Itcm,       // 0x00000000 32K
    Dtcm,       // 0x027E0000 16K
    Io,         // 0x04000000 64K
    Bios9,      // 0xFFFF0000 4K
    Bios7,      // 0x00000000 16K (overlaps Itcm, lower priority when Itcm enabled)
    Rom,        // 0x08000000 cart mirror (read-only)
    SharedWram, // 0x03000000 32K
    Unmapped
};

struct BusStats {
    uint64_t ram_reads = 0, ram_writes = 0;
    uint64_t itcm_reads = 0, itcm_writes = 0;
    uint64_t dtcm_reads = 0, dtcm_writes = 0;
    uint64_t io_reads = 0, io_writes = 0;
    uint64_t bios_reads = 0;
    uint64_t rom_reads = 0;
    uint64_t unmapped_reads = 0, unmapped_writes = 0;
    uint64_t cp15_reads = 0, cp15_writes = 0;
    uint64_t swi_count = 0;
    uint64_t unknown_cp15 = 0;
    uint64_t unknown_swi = 0;
};

// CP15 minimal state for boot
struct Cp15State {
    uint32_t control = 0x00000078; // reset value-ish, bit13 high vectors maybe
    uint32_t dtcm_base = 0x027E0000;
    uint32_t dtcm_size = 0x4000; // 16K
    uint32_t itcm_base = 0x00000000;
    uint32_t itcm_size = 0x8000; // 32K
    bool itcm_enable = true;
    bool dtcm_enable = true;
    bool mpu_enable = false;
    // MPU: 8 regions, base/size/enable
    struct MpuRegion { uint32_t base = 0; uint32_t size = 0; bool enable = false; };
    std::array<MpuRegion, 8> mpu{};
};

Cp15State& cp15_state();
BusStats& bus_stats();
void clear_bus_stats();

// Memory abstraction — separate regions, no direct DS address dereference.
void init_memory(const uint8_t* arm9_data, size_t arm9_size, uint32_t arm9_base);
// Extended init that also loads optional bios/rom blobs (may be nullptr)
void init_memory_ex(const uint8_t* arm9_data, size_t arm9_size, uint32_t arm9_base,
                    const uint8_t* bios9_data, size_t bios9_size,
                    const uint8_t* bios7_data, size_t bios7_size,
                    const uint8_t* rom_data, size_t rom_size);
void shutdown_memory();

// Raw access (used by HAL and interpreter)
uint8_t  read8(uint32_t addr);
uint16_t read16(uint32_t addr);
uint32_t read32(uint32_t addr);
void write8(uint32_t addr, uint8_t  val);
void write16(uint32_t addr, uint16_t val);
void write32(uint32_t addr, uint32_t val);

// Region query (for provenance / logging)
Region resolve_region(uint32_t addr);
const char* region_name(Region r);
bool is_io_region(uint32_t addr);
bool is_bios_region(uint32_t addr);

// Provenance per 4K page (Main RAM only for Phase 3.1)
bool has_write_provenance(uint32_t addr, uint32_t size);
void note_write_provenance(uint32_t addr, uint32_t size);

// HAL wrappers used by generated code (C linkage for generated C++).
extern "C" {
    uint32_t nds_hal_read32(uint32_t addr);
    uint16_t nds_hal_read16(uint32_t addr);
    uint8_t  nds_hal_read8(uint32_t addr);
    void nds_hal_write32(uint32_t addr, uint32_t val);
    void nds_hal_write16(uint32_t addr, uint16_t val);
    void nds_hal_write8(uint32_t addr, uint8_t val);
    void nds_unimplemented(uint32_t addr, uint32_t raw);
    void nds_swi(uint32_t id);
    void nds_bkpt(uint32_t id);
    uint32_t nds_mrc(uint32_t cp, uint32_t op2, uint32_t crn, uint32_t crm);
    void nds_mcr(uint32_t cp, uint32_t op2, uint32_t val, uint32_t crn, uint32_t crm);
}

// For generated C++ to report unimplemented execution
void set_unimplemented_handler(void (*handler)(uint32_t addr, uint32_t raw));
bool has_unimplemented_fault();
uint32_t fault_address();
uint32_t fault_raw();
void clear_fault();

// Trace helpers (called by interpreter)
void trace_cp15_access(bool is_read, uint32_t cp, uint32_t op2, uint32_t crn, uint32_t crm, uint32_t val);
void trace_io_access(bool is_read, uint32_t addr, uint32_t size, uint32_t val);
void trace_swi(uint32_t id, uint32_t pc);
const uint8_t* get_itcm_data();
size_t get_itcm_size();
const uint8_t* get_dtcm_data();
size_t get_dtcm_size();
const uint8_t* get_main_data();
size_t get_main_size();

} // namespace descomp::runtime
