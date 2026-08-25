#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <map>
#include <mutex>

namespace descomp::runtime {

// Dynamic code region - tracks code copied to RAM/ITCM/DTCM
struct DynamicCodeRegion {
    uint32_t source{0};                    // original ROM/RAM address
    uint32_t destination{0};               // ITCM/DTCM/RAM address (0x00000000 etc)
    uint32_t size{0};
    std::string permissions;               // "rwx" etc
    bool is_thumb{false};
    uint32_t discovered_functions{0};
    uint32_t discovered_blocks{0};
    uint64_t execution_count{0};
    uint64_t write_count{0};
    uint64_t generation{0};                // provenance generation for invalidation
};

class DynamicCodeManager {
public:
    static DynamicCodeManager& instance();

    // Called on every write to ITCM/DTCM/RAM that could be code
    // Also bumps generation to invalidate old recompiled code
    void note_write(uint32_t dest, uint32_t size, uint32_t source_hint = 0);

    // Called when PC tries to execute at an address not in static program
    // Decodes dynamic code from ITCM/DTCM, registers function, returns true if discovered
    bool ensure_function(uint32_t addr, bool thumb);

    // For JSON output
    std::vector<DynamicCodeRegion> regions() const { return regions_; }
    void clear();

    // For testing
    size_t region_count() const { return regions_.size(); }
    bool has_region(uint32_t dest) const;

    // Get current generation (for invalidation tracking)
    uint64_t current_generation() const { return generation_counter; }

private:
    DynamicCodeManager() = default;
    std::vector<DynamicCodeRegion> regions_;
    std::map<uint32_t, DynamicCodeRegion> region_map_; // key = dest
    uint64_t generation_counter{1}; // starts at 1, bumped on ITCM writes

    // Helpers to decode and register
    bool decode_and_register(uint32_t dest, bool thumb);
};

} // namespace descomp::runtime
