// Hardcoded DAE → Assimp → NeutralMesh → D3D11 renderer
#define _CRT_SECURE_NO_WARNINGS
#include <cstdio>
#include "model_view.h"
#include "assimp_loader.h"

int main() {
    const char* dae_path = "D:\\DescompDS\\test_export\\face_demo_mario.dae";

    printf("=== HARDCODED DAE RENDERER ===\n");
    printf("Loading: %s\n", dae_path);

    NeutralMesh mesh;
    if (!load_dae_to_neutral(dae_path, mesh)) {
        printf("FAILED to load DAE via Assimp\n");
        return 1;
    }

    printf("vertices: %zu\n", mesh.vertices.size());
    printf("indices: %zu\n", mesh.indices.size());
    printf("triangles: %zu\n", mesh.indices.size() / 3);
    printf("submeshes: %zu\n", mesh.submeshes.size());
    printf("materials: %zu\n", mesh.materials.size());
    printf("textures: %zu\n", mesh.texture_files.size());
    printf("bbox: (%.2f %.2f %.2f) - (%.2f %.2f %.2f)\n",
        mesh.bbox_min[0], mesh.bbox_min[1], mesh.bbox_min[2],
        mesh.bbox_max[0], mesh.bbox_max[1], mesh.bbox_max[2]);

    printf("Launching D3D11 window...\n");
    modelview::show_neutral(mesh, "DAE: face_demo_mario (Assimp → NeutralMesh → D3D11)");

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
