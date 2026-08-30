// DescompDS Explorer — ROM filesystem access + LZ77/LZ10 decompression
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace romfile {

// Read a FAT file from an NDS ROM.
// Returns false on error.
bool read_file(const std::string& rom_path, int fat_index, std::vector<uint8_t>& out_raw);

// Decompress LZ77/LZ10. Uncompressed data returned unchanged.
std::vector<uint8_t> decompress(const std::vector<uint8_t>& raw);

} // namespace romfile