#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <map>
#include <mutex>

namespace descomp::runtime {

// Subsystem access tracking - records reads/writes to DS subsystems
// Used to profile which subsystems are exercised by game code
struct SubsystemAccess {
    uint64_t dma_writes{0};
    uint64_t dma_reads{0};
    uint64_t timer_writes{0};
    uint64_t timer_reads{0};
    uint64_t irq_acks{0};
    uint64_t ipc_messages{0};
    uint64_t vram_writes{0};
    uint64_t vram_reads{0};
    uint64_t gpu_2d_writes{0};
    uint64_t gpu_2d_reads{0};
    uint64_t gpu_3d_writes{0};
    uint64_t gpu_3d_reads{0};
    uint64_t audio_writes{0};
    uint64_t audio_reads{0};
    uint64_t cartridge_writes{0};
    uint64_t cartridge_reads{0};
    uint64_t generation{0}; // provenance generation

    // Reset all counters
    void clear();

    // JSON serialization
    std::string to_json() const;

    // Get generation for invalidation tracking
    uint64_t get_generation() const { return generation; }
};

// Central access point - singleton to record subsystem accesses
class SubsystemAccessTracker {
public:
    static SubsystemAccessTracker& instance();

    // Record a DMA write (chan 0-3, size)
    void dma_write(int chan, uint32_t size);

    // Record a DMA read (chan 0-3, size)
    void dma_read(int chan, uint32_t size);

    // Record a timer access (timer 0-3, read/write, val optional)
    void timer_access(int timer, bool is_read, uint32_t val = 0);

    // Record an IRQ acknowledge
    void irq_acknowledge();

    // Record an IPC message send/receive
    void ipc_message(bool is_send, uint32_t size);

    // Record a VRAM access (read/write, size)
    void vram_access(bool is_read, uint32_t size);

    // Record a GPU 2D access
    void gpu_2d_access(bool is_read, uint32_t size);

    // Record a GPU 3D access
    void gpu_3d_access(bool is_read, uint32_t size);

    // Record an audio access
    void audio_access(bool is_read, uint32_t size);

    // Record a cartridge access (read/write, size)
    void cartridge_access(bool is_read, uint32_t size);

    // Get the current access summary
    SubsystemAccess summary() const;

    // Get generation for invalidation
    uint64_t generation() const { return access_gen; }

    // JSON output for all subsystems
    std::string to_json() const;

private:
    SubsystemAccessTracker() = default;
    SubsystemAccess access_;
    uint64_t access_gen{1};
    mutable std::mutex mutex_;
};

// Convenience inline functions for tracking macros (optional, can be used in memory bus)
inline void track_dma_write(int chan, uint32_t size) {
    SubsystemAccessTracker::instance().dma_write(chan, size);
}
inline void track_dma_read(int chan, uint32_t size) {
    SubsystemAccessTracker::instance().dma_read(chan, size);
}
inline void track_timer_access(int timer, bool is_read, uint32_t val = 0) {
    SubsystemAccessTracker::instance().timer_access(timer, is_read, val);
}
inline void track_irq_ack() {
    SubsystemAccessTracker::instance().irq_acknowledge();
}
inline void track_ipc_message(bool is_send, uint32_t size) {
    SubsystemAccessTracker::instance().ipc_message(is_send, size);
}
inline void track_vram_access(bool is_read, uint32_t size) {
    SubsystemAccessTracker::instance().vram_access(is_read, size);
}
inline void track_gpu_2d_access(bool is_read, uint32_t size) {
    SubsystemAccessTracker::instance().gpu_2d_access(is_read, size);
}
inline void track_gpu_3d_access(bool is_read, uint32_t size) {
    SubsystemAccessTracker::instance().gpu_3d_access(is_read, size);
}
inline void track_audio_access(bool is_read, uint32_t size) {
    SubsystemAccessTracker::instance().audio_access(is_read, size);
}
inline void track_cartridge_access(bool is_read, uint32_t size) {
    SubsystemAccessTracker::instance().cartridge_access(is_read, size);
}

} // namespace descomp::runtime

// Integration functions - called from nds_runtime.cpp memory bus
namespace descomp::runtime {
void track_io_access_integration(uint32_t addr, bool is_read, uint32_t size);
void track_vram_access_integration(uint32_t addr, bool is_read, uint32_t size);
void track_gpu_2d_access_integration(uint32_t addr, bool is_read, uint32_t size);
void track_gpu_3d_access_integration(uint32_t addr, bool is_read, uint32_t size);
void track_audio_access_integration(uint32_t addr, bool is_read, uint32_t size);
void track_cartridge_access_integration(uint32_t addr, bool is_read, uint32_t size);
void track_timer_access_integration(uint32_t addr, bool is_read, uint32_t val);
void track_ipc_access_integration(uint32_t addr, bool is_read, uint32_t val);
void track_ipc_message_integration(bool is_send, uint32_t size);
}