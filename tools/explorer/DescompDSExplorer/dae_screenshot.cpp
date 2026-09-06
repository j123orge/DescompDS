// Hardcoded DAE → Assimp → NeutralMesh → D3D11 — Stage B screenshot
#define _CRT_SECURE_NO_WARNINGS
#include <cstdio>
#include <fstream>
#include "model_view.h"
#include "d3d_view.h"
#include "assimp_loader.h"

int main() {
    const char* dae_path = "D:\\DescompDS\\test_export\\face_demo_mario.dae";
    const char* screenshot = "D:\\DescompDS\\test_export\\dae_renderer_stageB.bmp";

    printf("=== STAGE B TEXTURED DAE SCREENSHOT ===\n");
    printf("Loading: %s\n", dae_path);

    NeutralMesh mesh;
    if (!load_dae_to_neutral(dae_path, mesh)) {
        printf("FAILED to load DAE\n"); return 1;
    }

    printf("vertices: %zu  indices: %zu  triangles: %zu\n",
        mesh.vertices.size(), mesh.indices.size(), mesh.indices.size()/3);
    printf("submeshes: %zu  materials: %zu  texture files: %zu\n",
        mesh.submeshes.size(), mesh.materials.size(), mesh.texture_files.size());

    // Validate texture existence
    printf("\n=== TEXTURE LOAD VALIDATION ===\n");
    int loaded = 0, failed = 0;
    for (size_t i = 0; i < mesh.materials.size(); i++) {
        auto& m = mesh.materials[i];
        std::string full = "D:\\DescompDS\\test_export\\" + m.diffuse_tex;
        std::ifstream f(full, std::ios::binary | std::ios::ate);
        bool exists = f.good();
        size_t fsize = exists ? (size_t)f.tellg() : 0;
        if (exists) loaded++; else failed++;
        printf("[%zu] %s  tex=%s  exists=%s  size=%zu\n",
            i, m.name.c_str(), m.diffuse_tex.c_str(), exists ? "YES" : "NO", fsize);
    }
    printf("loaded: %d/%zu  failed: %d\n", loaded, mesh.materials.size(), failed);
    if (failed > 0) { printf("FAILED: missing textures\n"); return 1; }

    // Validate submesh -> material mapping
    printf("\n=== SUBMESH VALIDATION ===\n");
    int bad_mat = 0;
    for (size_t i = 0; i < mesh.submeshes.size(); i++) {
        int matid = mesh.submeshes[i].material_id;
        if (matid < 0 || matid >= (int)mesh.materials.size()) {
            if (bad_mat < 10) printf("  submesh[%zu] bad material_id=%d\n", i, matid);
            bad_mat++;
        }
    }
    printf("submeshes with bad material_id: %d/%zu\n", bad_mat, mesh.submeshes.size());

    // First 20 submesh -> material mappings
    printf("\nFirst 20 submesh mappings:\n");
    for (size_t i = 0; i < 20 && i < mesh.submeshes.size(); i++) {
        auto& sm = mesh.submeshes[i];
        int matid = sm.material_id;
        const char* texname = (matid >= 0 && matid < (int)mesh.materials.size())
            ? mesh.materials[matid].diffuse_tex.c_str() : "N/A";
        printf("  [%zu] firstIndex=%zu indexCount=%zu mat=%d tex=%s\n",
            i, sm.index_start, sm.index_count, matid, texname);
    }

    // UV range
    float minu=1e30f,maxu=-1e30f,minv=1e30f,maxv=-1e30f;
    for (auto& v : mesh.vertices) {
        if (v.uv[0]<minu) minu=v.uv[0]; if (v.uv[0]>maxu) maxu=v.uv[0];
        if (v.uv[1]<minv) minv=v.uv[1]; if (v.uv[1]>maxv) maxv=v.uv[1];
    }
    printf("\nUV range: u=[%.4f %.4f] v=[%.4f %.4f]\n", minu, maxu, minv, maxv);
    printf("V flip applied = NO\n");

    // Render
    printf("\n=== RENDERING ===\n");
    WNDCLASSW wc{};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"DaeScreenshot";
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(0, L"DaeScreenshot", L"Stage B",
        WS_OVERLAPPEDWINDOW, 0, 0, 900, 700, nullptr, nullptr, wc.hInstance, nullptr);

    if (!d3dview::init(hwnd)) { printf("D3D11 init FAILED\n"); return 1; }
    d3dview::load_neutral_mesh(mesh, 1);
    d3dview::reset_camera();
    d3dview::focus_model();

    d3dview::set_save_path(screenshot);
    d3dview::render();
    InvalidateRect(hwnd, nullptr, FALSE);
    UpdateWindow(hwnd);
    d3dview::render();

    printf("Screenshot saved: %s\n", screenshot);
    d3dview::shutdown();
    DestroyWindow(hwnd);

    printf("\nRENDER COMPLETED\n");
    printf("VISUAL VALIDATION = PENDING\n");
    return 0;
}
