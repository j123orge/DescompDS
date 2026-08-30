// DescompDS Explorer — SM64DS BMD parser implementation
#include "bmd.h"

#include <cstring>
#include <cmath>
#include <algorithm>
#include <cstdio>

namespace bmd {

namespace {
uint32_t rd32(const std::vector<uint8_t>& d, size_t o) {
    if (o + 4 > d.size()) return 0;
    return (uint32_t)d[o] | ((uint32_t)d[o + 1] << 8) |
           ((uint32_t)d[o + 2] << 16) | ((uint32_t)d[o + 3] << 24);
}
int32_t rds32(const std::vector<uint8_t>& d, size_t o) { return (int32_t)rd32(d, o); }
uint16_t rd16(const std::vector<uint8_t>& d, size_t o) {
    if (o + 2 > d.size()) return 0;
    return (uint16_t)(d[o] | (d[o + 1] << 8));
}
int16_t rds16(const std::vector<uint8_t>& d, size_t o) { return (int16_t)rd16(d, o); }
uint8_t rd8(const std::vector<uint8_t>& d, size_t o) { return o < d.size() ? d[o] : 0; }

std::string cstr(const std::vector<uint8_t>& d, size_t o) {
    if (o >= d.size()) return "";
    std::string s;
    while (o < d.size() && d[o] != 0 && s.size() < 256) { s += (char)d[o++]; }
    return s;
}

// ---- 4x4 row-major matrix helpers (matching SM64DSe/OpenTK convention) ----
// row-major: m[row*4+col]
void mat_mul(float* out, const float* a, const float* b) {
    float t[16];
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++) {
            t[r*4+c] = 0;
            for (int k = 0; k < 4; k++) t[r*4+c] += a[r*4+k] * b[k*4+c];
        }
    for (int i = 0; i < 16; i++) out[i] = t[i];
}
void mat_identity(float* m) { for (int i = 0; i < 16; i++) m[i] = 0; m[0]=m[5]=m[10]=m[15]=1; }
void mat_scale(float* m, float sx, float sy, float sz) {
    mat_identity(m); m[0]=sx; m[5]=sy; m[10]=sz;
}
void mat_translate(float* m, float tx, float ty, float tz) {
    mat_identity(m); m[12]=tx; m[13]=ty; m[14]=tz;
}
void mat_rotx(float* m, float a) {
    float c=cosf(a), s=sinf(a);
    mat_identity(m);
    m[5]=c; m[6]=-s; m[9]=s; m[10]=c;
}
void mat_roty(float* m, float a) {
    float c=cosf(a), s=sinf(a);
    mat_identity(m);
    m[0]=c; m[2]=s; m[8]=-s; m[10]=c;
}
void mat_rotz(float* m, float a) {
    float c=cosf(a), s=sinf(a);
    mat_identity(m);
    m[0]=c; m[1]=-s; m[4]=s; m[5]=c;
}
// M_local = S * (Rx * Ry * Rz) * T   (right-handed, rot in radians)
void srt_matrix(float* out, const float* scale, const short* rot, const float* trans) {
    float S[16], RX[16], RY[16], RZ[16], T[16], R[16], SR[16], RXRY[16];
    mat_scale(S, scale[0], scale[1], scale[2]);
    mat_rotx(RX, (float)rot[0] * 3.14159265358979f / 2048.0f);
    mat_roty(RY, (float)rot[1] * 3.14159265358979f / 2048.0f);
    mat_rotz(RZ, (float)rot[2] * 3.14159265358979f / 2048.0f);
    mat_translate(T, trans[0], trans[1], trans[2]);
    mat_mul(RXRY, RX, RY);
    mat_mul(R, RXRY, RZ);     // Rx*Ry*Rz
    mat_mul(SR, S, R);        // S*R
    mat_mul(out, SR, T);      // S*R*T
}
} // namespace

bool parse_model(const std::vector<uint8_t>& d, Model& out, std::string& err) {
    if (d.size() < 60) { err = "BMD too small (<60)"; return false; }

    int scale_shift = (int)rds32(d, 0x00);
    out.scale_shift = scale_shift;
    out.scale_factor = (float)(1 << scale_shift);

    int n_bones = (int)rds32(d, 0x04);
    uint32_t bones_off = rd32(d, 0x08);
    int n_poly = (int)rds32(d, 0x0C);
    uint32_t poly_off = rd32(d, 0x10);
    int n_tex = (int)rds32(d, 0x14);
    uint32_t tex_off = rd32(d, 0x18);
    int n_pal = (int)rds32(d, 0x1C);
    int n_mat = (int)rds32(d, 0x24);
    uint32_t mat_off = rd32(d, 0x28);
    uint32_t bonemap_off = rd32(d, 0x2C);

    // Bounds guards
    auto valid = [&](uint32_t off, uint32_t size) {
        return off < d.size() && size <= d.size() - off;
    };
    if (!valid(bones_off, (uint32_t)n_bones * 64)) { err = "bones section out of range"; return false; }

    // Bones
    out.bones.resize(n_bones);
    for (int i = 0; i < n_bones; i++) {
        size_t bo = bones_off + (size_t)i * 64;
        Bone& b = out.bones[i];
        b.id = (int)rds32(d, bo);
        b.name = cstr(d, rd32(d, bo + 4));
        b.parent = rds16(d, bo + 8);
        b.num_pairs = (int)rds32(d, bo + 0x30);
        b.scale[0] = (float)rds32(d, bo + 0x10) / 4096.0f;
        b.scale[1] = (float)rds32(d, bo + 0x14) / 4096.0f;
        b.scale[2] = (float)rds32(d, bo + 0x18) / 4096.0f;
        b.rot[0] = rds16(d, bo + 0x1C);
        b.rot[1] = rds16(d, bo + 0x1E);
        b.rot[2] = rds16(d, bo + 0x20);
        b.trans[0] = (float)rds32(d, bo + 0x24) / 4096.0f;
        b.trans[1] = (float)rds32(d, bo + 0x28) / 4096.0f;
        b.trans[2] = (float)rds32(d, bo + 0x2C) / 4096.0f;
    }

    // Accumulate bone world matrices (validated math):
    //   local = S * Rx * Ry * Rz * T (right-handed, rot*pi/2048)
    //   world = local * parent.world
    for (int i = 0; i < n_bones; i++) {
        Bone& b = out.bones[i];
        float local[16];
        srt_matrix(local, b.scale, b.rot, b.trans);
        int parent = b.parent;
        int pidx = (parent < 0) ? (i + parent) : -1;   // parent_offset<0 => previous bone
        if (pidx < 0 || pidx >= n_bones) {
            for (int k = 0; k < 16; k++) b.world[k] = local[k];
        } else {
            mat_mul(b.world, local, out.bones[pidx].world);  // local * parent.world
        }
    }

    // Bone map (header 0x2C): series of shorts (matrix index -> bone id)
    std::vector<uint16_t> bonemap;
    for (int k = 0; k < 128 && valid(bonemap_off + (uint32_t)k * 2, 2); k++)
        bonemap.push_back(rd16(d, bonemap_off + (uint32_t)k * 2));

    // Materials
    if (valid(mat_off, (uint32_t)n_mat * 48)) {
        out.materials.resize(n_mat);
        for (int i = 0; i < n_mat; i++) {
            size_t mo = mat_off + (size_t)i * 48;
            Material& m = out.materials[i];
            m.name = cstr(d, rd32(d, mo));
            m.tex_id = rd32(d, mo + 0x04);
            m.pal_id = rd32(d, mo + 0x08);
            m.tex_params = rd32(d, mo + 0x20);
            m.poly_attribs = rd32(d, mo + 0x24);
            m.dif_amb = rd32(d, mo + 0x28);
            m.spe_emi = rd32(d, mo + 0x2C);
        }
    }

    // Textures
    if (valid(tex_off, (uint32_t)n_tex * 20)) {
        out.textures.resize(n_tex);
        for (int i = 0; i < n_tex; i++) {
            size_t to = tex_off + (size_t)i * 20;
            Texture& t = out.textures[i];
            t.name = cstr(d, rd32(d, to));
            t.data_offset = rd32(d, to + 4);
            t.size = rd32(d, to + 8);
            t.width = (int)rd16(d, to + 0x0C);
            t.height = (int)rd16(d, to + 0x0E);
            t.params = rd32(d, to + 0x10);
        }
    }

    // Display lists -> geometry via GX decode
    for (int pi = 0; pi < n_poly; pi++) {
        size_t po = poly_off + (size_t)pi * 8;
        if (po + 8 > d.size()) break;
        uint32_t dl_off = rd32(d, po + 4);
        if (!valid(dl_off, 16)) continue;

        // display list header: [0]=nTransforms [4]=transOff [8]=dataSize [0xC]=dataOff
        uint32_t numtr = rd32(d, dl_off);
        uint32_t troff = rd32(d, dl_off + 4);
        uint32_t dlsize = rd32(d, dl_off + 8);
        uint32_t dloff = rd32(d, dl_off + 0x0C);
        if (!valid(dloff, dlsize)) continue;

        // Decode GX commands
        size_t pos = dloff, end = dloff + dlsize;
        // state
        Vertex cur;
        cur.matrix_id = 0;
        int cur_poly = -1;
        MaterialGroup group;
        group.poly_id = pi;
        Primitive* open_prim = nullptr;
        // bone_ids: matrix_id -> bone id (transform list bytes -> bone map shorts)
        for (uint32_t tb = 0; tb < numtr; tb++) {
            uint8_t idx1 = rd8(d, troff + tb);
            uint16_t bid = (idx1 < bonemap.size()) ? bonemap[idx1] : 0;
            group.bone_ids.push_back(bid < (uint32_t)n_bones ? (int)bid : 0);
        }

        while (pos + 4 <= end) {
            uint8_t c0 = d[pos], c1 = d[pos + 1], c2 = d[pos + 2], c3 = d[pos + 3];
            pos += 4;
            uint8_t cmds[4] = {c0, c1, c2, c3};
            for (uint8_t c : cmds) {
                switch (c) {
                    case 0x00: break; // nop
                    case 0x10: pos += 4; break;
                    case 0x11: break;
                    case 0x12: pos += 4; break;
                    case 0x13: pos += 4; break;
                    case 0x14: { // matrix restore
                        uint32_t param = rd32(d, pos); pos += 4;
                        cur.matrix_id = (int)(param & 0x1F);
                        break;
                    }
                    case 0x15: break;
                    case 0x16: pos += 64; break;
                    case 0x17: pos += 48; break;
                    case 0x18: pos += 64; break;
                    case 0x19: pos += 48; break;
                    case 0x1A: pos += 36; break;
                    case 0x1B: pos += 12; break;
                    case 0x1C: pos += 12; break;
                    case 0x20: { // color
                        uint32_t raw = rd32(d, pos); pos += 4;
                        cur.r = (uint8_t)((raw << 3) & 0xF8);
                        cur.g = (uint8_t)((raw >> 2) & 0xF8);
                        cur.b = (uint8_t)((raw >> 7) & 0xF8);
                        cur.a = 255;
                        break;
                    }
                    case 0x21: { // normal
                        uint32_t param = rd32(d, pos); pos += 4;
                        int16_t x = (int16_t)((param << 6) & 0xFFC0);
                        int16_t y = (int16_t)((param >> 4) & 0xFFC0);
                        int16_t z = (int16_t)((param >> 14) & 0xFFC0);
                        cur.nx = (float)x / 32768.0f; cur.ny = (float)y / 32768.0f; cur.nz = (float)z / 32768.0f;
                        break;
                    }
                    case 0x22: { // texcoord
                        uint32_t param = rd32(d, pos); pos += 4;
                        int16_t s = (int16_t)(param & 0xFFFF);
                        int16_t t = (int16_t)(param >> 16);
                        cur.u = (float)s / 16.0f; cur.v = (float)t / 16.0f;
                        break;
                    }
                    case 0x23: { // vertex XYZ (2 params)
                        uint32_t p1 = rd32(d, pos); pos += 4;
                        uint32_t p2 = rd32(d, pos); pos += 4;
                        int16_t x = (int16_t)(p1 & 0xFFFF);
                        int16_t y = (int16_t)(p1 >> 16);
                        int16_t z = (int16_t)(p2 & 0xFFFF);
                        Vertex v = cur;
                        v.x = (float)x / 4096.0f * out.scale_factor;
                        v.y = (float)y / 4096.0f * out.scale_factor;
                        v.z = (float)z / 4096.0f * out.scale_factor;
                        if (open_prim) { open_prim->verts.push_back(v); out.gx_vertices++; }
                        break;
                    }
                    case 0x24: { // vertex XYZ packed
                        uint32_t param = rd32(d, pos); pos += 4;
                        int16_t x = (int16_t)((param << 6) & 0xFFC0);
                        int16_t y = (int16_t)((param >> 4) & 0xFFC0);
                        int16_t z = (int16_t)((param >> 14) & 0xFFC0);
                        Vertex v = cur;
                        v.x = (float)x / 4096.0f * out.scale_factor;
                        v.y = (float)y / 4096.0f * out.scale_factor;
                        v.z = (float)z / 4096.0f * out.scale_factor;
                        if (open_prim) { open_prim->verts.push_back(v); out.gx_vertices++; }
                        break;
                    }
                    case 0x25: case 0x26: case 0x27: { // vertex 2-coord
                        uint32_t param = rd32(d, pos); pos += 4;
                        int16_t a = (int16_t)(param & 0xFFFF);
                        int16_t b = (int16_t)(param >> 16);
                        Vertex v = cur;
                        if (c == 0x25) { v.x = (float)a / 4096.0f * out.scale_factor; v.y = (float)b / 4096.0f * out.scale_factor; }
                        else if (c == 0x26) { v.x = (float)a / 4096.0f * out.scale_factor; v.z = (float)b / 4096.0f * out.scale_factor; }
                        else { v.y = (float)a / 4096.0f * out.scale_factor; v.z = (float)b / 4096.0f * out.scale_factor; }
                        if (open_prim) { open_prim->verts.push_back(v); out.gx_vertices++; }
                        break;
                    }
                    case 0x28: { // vertex delta
                        uint32_t param = rd32(d, pos); pos += 4;
                        int16_t x = (int16_t)((param << 6) & 0xFFC0);
                        int16_t y = (int16_t)((param >> 4) & 0xFFC0);
                        int16_t z = (int16_t)((param >> 14) & 0xFFC0);
                        Vertex v = cur;
                        v.x += (float)x / 262144.0f * out.scale_factor;
                        v.y += (float)y / 262144.0f * out.scale_factor;
                        v.z += (float)z / 262144.0f * out.scale_factor;
                        if (open_prim) { open_prim->verts.push_back(v); out.gx_vertices++; }
                        break;
                    }
                    case 0x29: case 0x2A: case 0x2B: pos += 4; break;
                    case 0x30: case 0x31: case 0x32: case 0x33: pos += 4; break;
                    case 0x34: pos += 128; break;
                    case 0x40: { // begin vertex list
                        uint32_t param = rd32(d, pos); pos += 4;
                        group.prims.push_back(Primitive{});
                        open_prim = &group.prims.back();
                        open_prim->type = (PrimType)(param & 0x3);
                        cur_poly = (int)(param & 0x3);
                        break;
                    }
                    case 0x41: { // end vertex list
                        if (cur_poly == 0) out.n_tri++;
                        else if (cur_poly == 1) out.n_quad++;
                        else if (cur_poly == 2) out.n_tristrip++;
                        else if (cur_poly == 3) out.n_quadstrip++;
                        cur_poly = -1;
                        open_prim = nullptr;
                        break;
                    }
                    case 0x50: pos += 4; break;
                    case 0x60: pos += 4; break;
                    case 0x70: pos += 12; break;
                    case 0x71: pos += 8; break;
                    case 0x72: pos += 4; break;
                    default: break;
                }
                if (pos > end) { pos = end; break; }
            }
        }
        if (!group.prims.empty()) out.groups.push_back(group);
    }

    // Compute bounding box over all geometry
    bool have = false;
    for (auto& g : out.groups) for (auto& p : g.prims) for (auto& v : p.verts) {
        if (!have) { out.minx = out.maxx = v.x; out.miny = out.maxy = v.y; out.minz = out.maxz = v.z; have = true; }
        else {
            out.minx = std::min(out.minx, v.x); out.maxx = std::max(out.maxx, v.x);
            out.miny = std::min(out.miny, v.y); out.maxy = std::max(out.maxy, v.y);
            out.minz = std::min(out.minz, v.z); out.maxz = std::max(out.maxz, v.z);
        }
    }
    out.has_bbox = have;

    // World bounding box (bone transforms applied — what the renderer shows)
    bool whave = false;
    for (auto& g : out.groups) for (auto& p : g.prims) for (auto& v : p.verts) {
        int bone_id = 0;
        if (v.matrix_id >= 0 && v.matrix_id < (int)g.bone_ids.size()) bone_id = g.bone_ids[v.matrix_id];
        if (bone_id < 0 || bone_id >= (int)out.bones.size()) bone_id = 0;
        const float* W = out.bones[bone_id].world;
        float x = v.x*W[0] + v.y*W[4] + v.z*W[8]  + W[12];
        float y = v.x*W[1] + v.y*W[5] + v.z*W[9]  + W[13];
        float z = v.x*W[2] + v.y*W[6] + v.z*W[10] + W[14];
        if (!whave) { out.wminx = out.wmaxx = x; out.wminy = out.wmaxy = y; out.wminz = out.wmaxz = z; whave = true; }
        else {
            out.wminx = std::min(out.wminx, x); out.wmaxx = std::max(out.wmaxx, x);
            out.wminy = std::min(out.wminy, y); out.wmaxy = std::max(out.wmaxy, y);
            out.wminz = std::min(out.wminz, z); out.wmaxz = std::max(out.wmaxz, z);
        }
    }
    out.has_wbbox = whave;
    return true;
}

} // namespace bmd