// Verify NeutralMesh geometry data from Assimp
#define _CRT_SECURE_NO_WARNINGS
#include <cstdio>
#include "assimp_loader.h"

int main() {
    const char* dae_path = "D:\\DescompDS\\test_export\\face_demo_mario.dae";
    NeutralMesh mesh;
    if (!load_dae_to_neutral(dae_path, mesh)) { printf("FAILED\n"); return 1; }

    printf("=== NEUTRALMESH VERIFICATION ===\n");
    printf("vertices: %zu\n", mesh.vertices.size());
    printf("indices: %zu\n", mesh.indices.size());
    printf("triangles: %zu\n", mesh.indices.size() / 3);
    printf("submeshes: %zu\n", mesh.submeshes.size());
    printf("materials: %zu\n", mesh.materials.size());
    printf("textures: %zu\n", mesh.texture_files.size());
    printf("bbox_min: (%.4f %.4f %.4f)\n", mesh.bbox_min[0], mesh.bbox_min[1], mesh.bbox_min[2]);
    printf("bbox_max: (%.4f %.4f %.4f)\n", mesh.bbox_max[0], mesh.bbox_max[1], mesh.bbox_max[2]);
    float dx=mesh.bbox_max[0]-mesh.bbox_min[0], dy=mesh.bbox_max[1]-mesh.bbox_min[1], dz=mesh.bbox_max[2]-mesh.bbox_min[2];
    printf("bbox_size: (%.4f %.4f %.4f)\n", dx, dy, dz);

    // First 5 vertices
    printf("\nFirst 5 vertices:\n");
    for (int i = 0; i < 5 && i < (int)mesh.vertices.size(); i++) {
        auto& v = mesh.vertices[i];
        printf("  [%d] pos=(%.4f %.4f %.4f) uv=(%.4f %.4f) normal=(%.4f %.4f %.4f) color=(%d %d %d %d)\n",
            i, v.pos[0], v.pos[1], v.pos[2], v.uv[0], v.uv[1],
            v.normal[0], v.normal[1], v.normal[2],
            v.color[0], v.color[1], v.color[2], v.color[3]);
    }

    // First 3 triangles
    printf("\nFirst 3 triangles (indices):\n");
    for (int i = 0; i < 3 && i*3+2 < (int)mesh.indices.size(); i++) {
        uint32_t a = mesh.indices[i*3], b = mesh.indices[i*3+1], c = mesh.indices[i*3+2];
        printf("  [%d] %u %u %u\n", i, a, b, c);
        printf("       posA=(%.4f %.4f %.4f)\n", mesh.vertices[a].pos[0], mesh.vertices[a].pos[1], mesh.vertices[a].pos[2]);
        printf("       posB=(%.4f %.4f %.4f)\n", mesh.vertices[b].pos[0], mesh.vertices[b].pos[1], mesh.vertices[b].pos[2]);
        printf("       posC=(%.4f %.4f %.4f)\n", mesh.vertices[c].pos[0], mesh.vertices[c].pos[1], mesh.vertices[c].pos[2]);
    }

    // First 5 submeshes
    printf("\nFirst 5 submeshes:\n");
    for (int i = 0; i < 5 && i < (int)mesh.submeshes.size(); i++) {
        auto& sm = mesh.submeshes[i];
        printf("  [%d] index_start=%zu index_count=%zu material_id=%d\n", i, sm.index_start, sm.index_count, sm.material_id);
    }

    // First 5 materials
    printf("\nFirst 5 materials:\n");
    for (int i = 0; i < 5 && i < (int)mesh.materials.size(); i++) {
        auto& m = mesh.materials[i];
        printf("  [%d] name=%s diffuse=(%.2f %.2f %.2f) tex=%s\n",
            i, m.name.c_str(), m.diffuse[0], m.diffuse[1], m.diffuse[2], m.diffuse_tex.c_str());
    }

    // Validate all indices in range
    uint32_t bad = 0;
    for (auto idx : mesh.indices) if (idx >= mesh.vertices.size()) bad++;
    printf("\nindices out of range: %u\n", bad);

    // Validate triangle face count
    printf("expected indices: %zu (1798 * 3)\n", (size_t)1798 * 3);
    printf("actual indices: %zu\n", mesh.indices.size());

    // Color distribution: how many vertices have non-white color
    int has_color = 0, all_white = 0;
    for (auto& v : mesh.vertices) {
        if (v.color[0] != 255 || v.color[1] != 255 || v.color[2] != 255) has_color++;
        else all_white++;
    }
    printf("\ncolors: %d non-white, %d white\n", has_color, all_white);

    printf("\nNEUTRALMESH VERIFICATION = DONE\n");
    return 0;
}
