// Material data audit — check texture paths exist
#define _CRT_SECURE_NO_WARNINGS
#include <cstdio>
#include <fstream>
#include "assimp_loader.h"

int main() {
    const char* dae_path = "D:\\DescompDS\\test_export\\face_demo_mario.dae";
    NeutralMesh mesh;
    if (!load_dae_to_neutral(dae_path, mesh)) { printf("FAILED\n"); return 1; }

    printf("=== MATERIAL AUDIT ===\n");
    printf("materials: %zu\n", mesh.materials.size());
    printf("texture files: %zu\n", mesh.texture_files.size());

    int missing = 0;
    for (size_t i = 0; i < mesh.materials.size(); i++) {
        auto& m = mesh.materials[i];
        std::string full = "D:\\DescompDS\\test_export\\" + m.diffuse_tex;
        std::ifstream f(full, std::ios::binary);
        bool exists = f.good();
        if (!exists) missing++;
        printf("[%zu] %s  tex=%s  exists=%s\n",
            i, m.name.c_str(), m.diffuse_tex.c_str(), exists ? "YES" : "NO");
    }
    printf("\nmissing textures: %d\n", missing);

    // UV range check
    float minu=1e30f,maxu=-1e30f,minv=1e30f,maxv=-1e30f;
    for (auto& v : mesh.vertices) {
        if (v.uv[0]<minu) minu=v.uv[0]; if (v.uv[0]>maxu) maxu=v.uv[0];
        if (v.uv[1]<minv) minv=v.uv[1]; if (v.uv[1]>maxv) maxv=v.uv[1];
    }
    printf("\nUV range: u=[%.4f %.4f] v=[%.4f %.4f]\n", minu, maxu, minv, maxv);

    return 0;
}
