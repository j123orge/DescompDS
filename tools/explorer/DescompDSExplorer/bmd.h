// DescompDS Explorer — SM64DS BMD model parser
// Independent implementation based on the publicly documented BMD format
// (SM64DSe bmd_format.txt / observed behavior). Not a copy of GPL code.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace bmd {

// ---- Intermediate Representation (IR) -------------------------------------

struct Vertex {
    float x = 0, y = 0, z = 0;   // position (world, pre-bone)
    float nx = 0, ny = 0, nz = 0;
    float u = 0, v = 0;
    uint8_t r = 255, g = 255, b = 255, a = 255;
    int matrix_id = 0;           // bone/matrix reference (skinning)
};

// Primitive type, matching DS BEGIN param & 3
enum class PrimType { Triangles = 0, Quads = 1, TriangleStrip = 2, QuadStrip = 3 };

struct Primitive {
    PrimType type = PrimType::Triangles;
    std::vector<Vertex> verts;
};

struct Bone {
    int id = 0;
    std::string name;
    int parent = 0;             // parent bone index (-1 = none; parent_offset<0 => idx+parent)
    int num_pairs = 0;
    float scale[3] = {1, 1, 1};
    short rot[3] = {0, 0, 0};   // 0x0400 = 90 deg
    float trans[3] = {0, 0, 0};
    // Accumulated world matrix (4x4, row-major). Computed during parse:
    //   local = S * Rx * Ry * Rz * T  (right-handed, rot*pi/2048)
    //   world = local * parent.world
    float world[16] = {
        1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1
    };
};

struct Material {
    std::string name;
    uint32_t tex_id = 0xFFFFFFFF;
    uint32_t pal_id = 0xFFFFFFFF;
    uint32_t tex_params = 0;
    uint32_t poly_attribs = 0;
    uint32_t dif_amb = 0;
    uint32_t spe_emi = 0;
};

struct Texture {
    std::string name;
    uint32_t data_offset = 0;
    uint32_t size = 0;
    int width = 0, height = 0;
    uint32_t params = 0;
    std::vector<uint8_t> decoded; // width*height*4 BGRA
};

struct MaterialGroup {
    int mat_id = 0;
    int poly_id = 0;
    std::string mat_name;
    uint32_t poly_attribs = 0;
    int bone_index = 0;
    uint32_t tex_id = 0xFFFFFFFF;
    // matrix_id -> bone id (from the display list's transform list + bone map)
    std::vector<int> bone_ids;
    std::vector<Primitive> prims;   // geometry for this group
    float tex_scale_u = 1.0f, tex_scale_v = 1.0f;
    float tex_trans_u = 0.0f, tex_trans_v = 0.0f;
};

struct Model {
    int scale_shift = 0;         // scale factor = 1 << scale_shift
    float scale_factor = 1.0f;
    std::vector<Bone> bones;
    std::vector<Material> materials;
    std::vector<Texture> textures;
    std::vector<MaterialGroup> groups;   // flattened (mat,poly) pairs
    // aggregate stats
    int gx_vertices = 0;
    int n_tri = 0, n_quad = 0, n_tristrip = 0, n_quadstrip = 0;

    // bounding box (raw, local space)
    bool has_bbox = false;
    float minx = 0, miny = 0, minz = 0, maxx = 0, maxy = 0, maxz = 0;
    // world bounding box (bone transforms applied) — what the renderer actually shows
    bool has_wbbox = false;
    float wminx = 0, wminy = 0, wminz = 0, wmaxx = 0, wmaxy = 0, wmaxz = 0;
};

// ---- Parsing ---------------------------------------------------------------

// Parse a decompressed BMD blob into an IR model.
// Returns false and fills err on failure.
bool parse_model(const std::vector<uint8_t>& data, Model& out, std::string& err);

} // namespace bmd