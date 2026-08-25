#include "runtime/subsystem_access.h"
#include "runtime/nds_runtime.h"
#include <cstring>
#include <algorithm>
#include <cinttypes>

namespace descomp::runtime {

// --- SubsystemAccess methods ---

void SubsystemAccess::clear() {
    std::memset(this, 0, sizeof(SubsystemAccess));
}

std::string SubsystemAccess::to_json() const {
    // Simple JSON output for subsystem access summary
    // In a full implementation this would include address ranges, frequencies, etc.
    char buf[512];
    snprintf(buf, sizeof(buf),
        "{"
        "\"dma_writes\":%" PRIu64 ","
        "\"dma_reads\":%" PRIu64 ","
        "\"timer_writes\":%" PRIu64 ","
        "\"timer_reads\":%" PRIu64 ","
        "\"irq_acks\":%" PRIu64 ","
        "\"ipc_messages\":%" PRIu64 ","
        "\"vram_writes\":%" PRIu64 ","
        "\"vram_reads\":%" PRIu64 ","
        "\"gpu_2d_writes\":%" PRIu64 ","
        "\"gpu_2d_reads\":%" PRIu64 ","
        "\"gpu_3d_writes\":%" PRIu64 ","
        "\"gpu_3d_reads\":%" PRIu64 ","
        "\"audio_writes\":%" PRIu64 ","
        "\"audio_reads\":%" PRIu64 ","
        "\"cartridge_writes\":%" PRIu64 ","
        "\"cartridge_reads\":%" PRIu64 ","
        "\"generation\":%" PRIu64 ""
        "}",
        dma_writes, dma_reads, timer_writes, timer_reads, irq_acks, ipc_messages,
        vram_writes, vram_reads, gpu_2d_writes, gpu_2d_reads, gpu_3d_writes, gpu_3d_reads,
        audio_writes, audio_reads, cartridge_writes, cartridge_reads, generation);
    return std::string(buf);
}

// --- SubsystemAccessTracker methods ---

SubsystemAccessTracker& SubsystemAccessTracker::instance() {
    static SubsystemAccessTracker inst;
    return inst;
}

void SubsystemAccessTracker::dma_write(int chan, uint32_t size) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (chan >= 0 && chan <= 3) access_.dma_writes += size;
}

void SubsystemAccessTracker::dma_read(int chan, uint32_t size) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (chan >= 0 && chan <= 3) access_.dma_reads += size;
}

void SubsystemAccessTracker::timer_access(int timer, bool is_read, uint32_t /*val*/) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (timer >= 0 && timer <= 3) {
        if (is_read) access_.timer_reads++;
        else access_.timer_writes++;
    }
}

void SubsystemAccessTracker::irq_acknowledge() {
    std::lock_guard<std::mutex> lock(mutex_);
    access_.irq_acks++;
}

void SubsystemAccessTracker::ipc_message(bool /*is_send*/, uint32_t /*size*/) {
    std::lock_guard<std::mutex> lock(mutex_);
    access_.ipc_messages++;
}

void SubsystemAccessTracker::vram_access(bool is_read, uint32_t size) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (is_read) access_.vram_reads += size;
    else access_.vram_writes += size;
}

void SubsystemAccessTracker::gpu_2d_access(bool is_read, uint32_t size) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (is_read) access_.gpu_2d_reads += size;
    else access_.gpu_2d_writes += size;
}

void SubsystemAccessTracker::gpu_3d_access(bool is_read, uint32_t size) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (is_read) access_.gpu_3d_reads += size;
    else access_.gpu_3d_writes += size;
}

void SubsystemAccessTracker::audio_access(bool is_read, uint32_t size) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (is_read) access_.audio_reads += size;
    else access_.audio_writes += size;
}

void SubsystemAccessTracker::cartridge_access(bool is_read, uint32_t size) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (is_read) access_.cartridge_reads += size;
    else access_.cartridge_writes += size;
}

SubsystemAccess SubsystemAccessTracker::summary() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return access_;
}

std::string SubsystemAccessTracker::to_json() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return access_.to_json();
}

// --- Integration with MemoryBus / nds_runtime ---

// These functions should be called from the MemoryBus/MMIO access patterns
// to track which subsystems are being accessed

// DMA write tracking - chan is 0-3, addr is the DMA control register address
void track_dma_write_integration(int chan, uint32_t addr, uint32_t size, const uint8_t* data) {
    (void)addr; (void)data;
    SubsystemAccessTracker::instance().dma_write(chan, size);
}

// DMA read tracking
void track_dma_read_integration(int chan, uint32_t addr, uint32_t size, uint8_t* data) {
    (void)addr; (void)data;
    SubsystemAccessTracker::instance().dma_read(chan, size);
}

// IO register access tracking - addr in IO region 0x04000000-0x04000400
void track_io_access_integration(uint32_t addr, bool is_read, uint32_t size) {
    (void)addr; (void)is_read; (void)size;
    // Could track specific registers here (DMA, IRQ, etc.)
    // For now, just increment general accesses
    SubsystemAccessTracker::instance().irq_acknowledge(); // placeholder
}

// VRAM access tracking - addr in VRAM region 0x06000000-0x060FFFFF
void track_vram_access_integration(uint32_t addr, bool is_read, uint32_t size) {
    (void)addr;
    SubsystemAccessTracker::instance().vram_access(is_read, size);
}

// GPU 2D access tracking - registers at 0x04000000 area
void track_gpu_2d_access_integration(uint32_t addr, bool is_read, uint32_t size) {
    (void)addr;
    SubsystemAccessTracker::instance().gpu_2d_access(is_read, size);
}

// GPU 3D access tracking - registers at 0x04000000 area
void track_gpu_3d_access_integration(uint32_t addr, bool is_read, uint32_t size) {
    (void)addr;
    SubsystemAccessTracker::instance().gpu_3d_access(is_read, size);
}

// Audio access tracking - registers at 0x04000000 area
void track_audio_access_integration(uint32_t addr, bool is_read, uint32_t size) {
    (void)addr;
    SubsystemAccessTracker::instance().audio_access(is_read, size);
}

// Cartridge access tracking - ROM region
void track_cartridge_access_integration(uint32_t addr, bool is_read, uint32_t size) {
    (void)addr;
    SubsystemAccessTracker::instance().cartridge_access(is_read, size);
}

// Timer access tracking - registers at 0x04000100 area
void track_timer_access_integration(uint32_t addr, bool is_read, uint32_t val) {
    (void)addr; (void)val;
    // Determine timer number from address
    // Timer 0: 0x04000100, Timer 1: 0x04000104, Timer 2: 0x04000108, Timer 3: 0x0400010C
    int timer = -1;
    if (addr >= 0x04000100 && addr < 0x04000110) {
        timer = (addr - 0x04000100) / 4;
    }
    track_timer_access(timer, is_read, val);
}

// IPC message tracking convenience
void track_ipc_message_integration(bool is_send, uint32_t size) {
    SubsystemAccessTracker::instance().ipc_message(is_send, size);
}

// IPC access tracking - registers at 0x040000B0 area
void track_ipc_access_integration(uint32_t addr, bool is_read, uint32_t val) {
    (void)addr; (void)val;
    // IPC is at 0x040000B0-0x040000FC
    track_ipc_message_integration(!is_read, 4); // simplified: ack on write
}

} // namespace descomp::runtime