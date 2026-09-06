// Quick check: do Assimp meshes actually have vertex color data?
#define _CRT_SECURE_NO_WARNINGS
#include <cstdio>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

int main() {
    Assimp::Importer imp;
    const aiScene* sc = imp.ReadFile("D:\\DescompDS\\test_export\\face_demo_mario.dae",
        aiProcess_Triangulate | aiProcess_JoinIdenticalVertices);
    if (!sc) { printf("FAILED\n"); return 1; }

    printf("meshes: %u\n", sc->mNumMeshes);
    int has_colors = 0, has_nonwhite = 0;
    for (unsigned i = 0; i < sc->mNumMeshes; i++) {
        const aiMesh* m = sc->mMeshes[i];
        if (m->HasVertexColors(0)) {
            has_colors++;
            for (unsigned v = 0; v < m->mNumVertices && v < 3; v++) {
                aiColor4D c = m->mColors[0][v];
                printf("  mesh[%u] vert[%u] color=(%.3f %.3f %.3f %.3f)\n",
                    i, v, c.r, c.g, c.b, c.a);
                if (c.r < 0.99f || c.g < 0.99f || c.b < 0.99f) has_nonwhite++;
            }
        }
    }
    printf("meshes with HasVertexColors(0): %d\n", has_colors);
    printf("vertices with non-white color: %d\n", has_nonwhite);
    return 0;
}
