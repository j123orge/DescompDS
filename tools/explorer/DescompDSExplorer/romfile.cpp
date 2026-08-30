// DescompDS Explorer — ROM filesystem access + LZ77/LZ10 decompression
#include "romfile.h"

#include <fstream>

namespace romfile {

namespace {

uint32_t rd32(const std::vector<uint8_t>& d, uint32_t o) {
    if (o + 4 > d.size()) return 0;
    return (uint32_t)d[o] | ((uint32_t)d[o + 1] << 8) |
           ((uint32_t)d[o + 2] << 16) | ((uint32_t)d[o + 3] << 24);
}

std::vector<uint8_t> lz10(const std::vector<uint8_t>& d, uint32_t expected) {
    std::vector<uint8_t> out;
    out.reserve(expected);
    size_t i = 0, n = d.size();
    while (i < n && out.size() < expected) {
        uint8_t flags = d[i++];
        for (int bit = 0; bit < 8; bit++) {
            if (i >= n || out.size() >= expected) break;
            if (flags & (0x80 >> bit)) {
                if (i + 1 >= n) return out;
                uint8_t b1 = d[i], b2 = d[i + 1];
                i += 2;
                uint32_t count = ((b1 >> 4) & 0xF) + 3;
                uint32_t disp = ((b1 & 0xF) << 8) | b2;
                if (disp >= out.size()) return out;
                for (uint32_t k = 0; k < count && out.size() < expected; k++)
                    out.push_back(out[out.size() - disp - 1]);
            } else {
                out.push_back(d[i++]);
            }
        }
    }
    return out;
}

} // namespace

bool read_file(const std::string& rom_path, int fat_index, std::vector<uint8_t>& out_raw) {
    std::ifstream f(rom_path, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    size_t sz = (size_t)f.tellg();
    f.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(sz);
    f.read((char*)data.data(), (std::streamsize)sz);

    uint32_t fat_off = rd32(data, 0x48);
    uint32_t start = rd32(data, fat_off + (uint32_t)fat_index * 8);
    uint32_t end = rd32(data, fat_off + (uint32_t)fat_index * 8 + 4);
    if (end <= start || end > data.size()) return false;
    out_raw.assign(data.begin() + start, data.begin() + end);
    return true;
}

std::vector<uint8_t> decompress(const std::vector<uint8_t>& raw) {
    if (raw.size() >= 4 && raw[0] == 'L' && raw[1] == 'Z' && raw[2] == '7' && raw[3] == '7') {
        const std::vector<uint8_t> st(raw.begin() + 4, raw.end());
        if (st.size() >= 4 && st[0] == 0x10) {
            uint32_t expected = st[1] | (st[2] << 8) | (st[3] << 16);
            return lz10(std::vector<uint8_t>(st.begin() + 4, st.end()), expected);
        }
        return st;
    }
    if (raw.size() >= 4 && (raw[0] == 0x10 || raw[0] == 0x00)) {
        uint32_t expected = raw[1] | (raw[2] << 8) | (raw[3] << 16);
        return lz10(std::vector<uint8_t>(raw.begin() + 4, raw.end()), expected);
    }
    return raw;
}

} // namespace romfile