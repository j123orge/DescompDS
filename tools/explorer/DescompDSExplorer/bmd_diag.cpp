// DescompDS Explorer — BMD diagnostic / validation CLI (Stage 1-2)
// Reads a SM64DS ROM, decompresses a FAT file, parses the BMD, prints
// diagnostics and optionally exports OBJ geometry.
//
// Usage: bmd_diag <rom.nds> <fat_index> [--obj out.obj]
#include "bmd.h"

#include <cstdio>
#include <cstring>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace {

struct RomFs {
    std::vector<uint8_t> data;
    uint32_t fat_off = 0;
    uint32_t fat_size = 0;
    bool load(const char* path) {
        std::ifstream f(path, std::ios::binary);
        if (!f) return false;
        f.seekg(0, std::ios::end);
        size_t sz = (size_t)f.tellg();
        f.seekg(0, std::ios::beg);
        data.resize(sz);
        f.read((char*)data.data(), (std::streamsize)sz);
        fat_off = rd32(0x48);
        fat_size = rd32(0x4C);
        return true;
    }
    uint8_t rd8(uint32_t o) const { return o < data.size() ? data[o] : 0; }
    uint32_t rd32(uint32_t o) const {
        if (o + 4 > data.size()) return 0;
        return (uint32_t)data[o] | ((uint32_t)data[o + 1] << 8) |
               ((uint32_t)data[o + 2] << 16) | ((uint32_t)data[o + 3] << 24);
    }
    std::pair<uint32_t, uint32_t> bounds(int idx) const {
        return {rd32(fat_off + (uint32_t)idx * 8), rd32(fat_off + (uint32_t)idx * 8 + 4)};
    }
    std::vector<uint8_t> get(int idx) const {
        auto [s, e] = bounds(idx);
        if (e <= s || e > data.size()) return {};
        return std::vector<uint8_t>(data.begin() + s, data.begin() + e);
    }
};

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

void export_obj(const bmd::Model& m, const char* path) {
    std::ofstream o(path);
    if (!o) { std::fprintf(stderr, "cannot open %s\n", path); return; }
    o << "# SM64DS BMD export (validation)\n";
    o << "o Mario\n";
    uint32_t vbase = 1;
    for (auto& g : m.groups) {
        for (auto& p : g.prims) {
            for (auto& v : p.verts)
                o << "v " << v.x << ' ' << v.y << ' ' << v.z << "\n";
            uint32_t n = (uint32_t)p.verts.size();
            if (p.type == bmd::PrimType::Triangles) {
                for (uint32_t i = 0; i + 2 < n; i += 3)
                    o << "f " << (vbase + i) << ' ' << (vbase + i + 1) << ' ' << (vbase + i + 2) << "\n";
            } else if (p.type == bmd::PrimType::TriangleStrip) {
                for (uint32_t i = 0; i + 2 < n; i++)
                    o << "f " << (vbase + i) << ' ' << (vbase + i + 1) << ' ' << (vbase + i + 2) << "\n";
            } else if (p.type == bmd::PrimType::Quads) {
                for (uint32_t i = 0; i + 3 < n; i += 4) {
                    o << "f " << (vbase + i) << ' ' << (vbase + i + 1) << ' ' << (vbase + i + 2) << "\n";
                    o << "f " << (vbase + i) << ' ' << (vbase + i + 2) << ' ' << (vbase + i + 3) << "\n";
                }
            } else if (p.type == bmd::PrimType::QuadStrip) {
                for (uint32_t i = 2; i + 1 < n; i += 2) {
                    o << "f " << (vbase + i - 2) << ' ' << (vbase + i) << ' ' << (vbase + i + 1) << "\n";
                    o << "f " << (vbase + i - 2) << ' ' << (vbase + i + 1) << ' ' << (vbase + i - 1) << "\n";
                }
            }
            vbase += n;
        }
    }
    o.close();
    std::printf("OBJ exported: %s\n", path);
}

void print_diag(const bmd::Model& m) {
    std::printf("=== SM64DS BMD diagnostic ===\n");
    std::printf("bones        : %zu\n", m.bones.size());
    std::printf("materials    : %zu\n", m.materials.size());
    std::printf("textures     : %zu\n", m.textures.size());
    std::printf("displaylists : %zu\n", m.groups.size());
    std::printf("GX vertices  : %d\n", m.gx_vertices);
    std::printf("primitives   : tri=%d quad=%d tristrip=%d quadstrip=%d\n",
                m.n_tri, m.n_quad, m.n_tristrip, m.n_quadstrip);
    if (m.has_bbox)
        std::printf("bbox         : (%.2f,%.2f,%.2f) - (%.2f,%.2f,%.2f)\n",
                    m.minx, m.miny, m.minz, m.maxx, m.maxy, m.maxz);
    std::printf("\nbones:\n");
    for (auto& b : m.bones)
        std::printf("  [%d] %s parent=%d pairs=%d\n", b.id, b.name.c_str(), b.parent, b.num_pairs);
    std::printf("\nmaterials:\n");
    for (size_t i = 0; i < m.materials.size(); i++)
        std::printf("  [%zu] %s tex=0x%X pal=0x%X\n", i, m.materials[i].name.c_str(),
                    m.materials[i].tex_id, m.materials[i].pal_id);
    std::printf("\ntextures:\n");
    for (size_t i = 0; i < m.textures.size(); i++)
        std::printf("  [%zu] %s %dx%d\n", i, m.textures[i].name.c_str(),
                    m.textures[i].width, m.textures[i].height);
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::printf("usage: bmd_diag <rom.nds> <fat_index> [--obj out.obj]\n");
        return 2;
    }
    RomFs fs;
    if (!fs.load(argv[1])) { std::fprintf(stderr, "cannot open %s\n", argv[1]); return 1; }
    int idx = std::atoi(argv[2]);

    auto raw = fs.get(idx);
    auto dec = decompress(raw);
    std::printf("file[%d]: raw=%zu decompressed=%zu\n", idx, raw.size(), dec.size());

    bmd::Model m;
    std::string err;
    if (!bmd::parse_model(dec, m, err)) {
        std::fprintf(stderr, "BMD parse failed: %s\n", err.c_str());
        return 1;
    }
    print_diag(m);

    for (int i = 3; i < argc; i++) {
        if (std::strcmp(argv[i], "--obj") == 0 && i + 1 < argc)
            export_obj(m, argv[i + 1]);
    }
    return 0;
}