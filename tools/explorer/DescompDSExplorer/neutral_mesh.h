// DescompDS Explorer — NeutralMesh: DS-agnostic mesh representation
// Loaded from OBJ files exported by SM64DSe. No GX/BMD/bone knowledge.
#pragma once

#include <vector>
#include <string>
#include <cstdint>

struct NeutralVertex {
    float pos[3];
    float uv[2];
    float normal[3];
    uint8_t color[4]; // RGBA, default white
};

struct NeutralMaterial {
    std::string name;
    float diffuse[3];
    float ambient[3];
    float specular[3];
    float alpha;
    std::string diffuse_tex; // texture filename from MTL
};

struct NeutralSubmesh {
    size_t index_start;
    size_t index_count;
    int material_id;
};

struct NeutralMesh {
    std::vector<NeutralVertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<NeutralMaterial> materials;
    std::vector<NeutralSubmesh> submeshes;
    std::vector<std::string> texture_files; // PNG filenames

    float bbox_min[3] = { 1e30f, 1e30f, 1e30f };
    float bbox_max[3] = { -1e30f, -1e30f, -1e30f };

    void clear() {
        vertices.clear();
        indices.clear();
        materials.clear();
        submeshes.clear();
        texture_files.clear();
        bbox_min[0] = bbox_min[1] = bbox_min[2] = 1e30f;
        bbox_max[0] = bbox_max[1] = bbox_max[2] = -1e30f;
    }

    bool empty() const { return vertices.empty(); }
};

// Load an OBJ file into a NeutralMesh.
// obj_path: full path to .obj file
// out: output mesh
// Returns true on success
bool load_obj_to_neutral(const char* obj_path, NeutralMesh& out);

// Export a NeutralMesh to a BMP snapshot for debugging
bool save_neutral_mesh_info(const NeutralMesh& m, const char* path);
