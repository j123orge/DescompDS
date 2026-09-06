// Assimp DAE validation — OFFICIAL headers, OFFICIAL lib
#define _CRT_SECURE_NO_WARNINGS
#include <cstdio>
#include <cstdlib>
#include <string>
#include <functional>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/version.h>

void countNodes(aiNode* n, unsigned int& out) {
    out++;
    for (unsigned int i = 0; i < n->mNumChildren; i++)
        countNodes(n->mChildren[i], out);
}

int main(int argc, char** argv) {
    const char* dae_path = argc > 1 ? argv[1] : "D:\\DescompDS\\test_export\\face_demo_mario.dae";

    printf("=== ASSIMP OFFICIAL DAE VALIDATION ===\n");
    printf("DAE path: %s\n", dae_path);

    printf("Assimp version: %u.%u.%u\n",
        aiGetVersionMajor(), aiGetVersionMinor(), aiGetVersionPatch());

    Assimp::Importer importer;

    unsigned int flags = aiProcess_Triangulate | aiProcess_JoinIdenticalVertices;
    printf("Import flags: aiProcess_Triangulate | aiProcess_JoinIdenticalVertices\n");

    const aiScene* scene = importer.ReadFile(dae_path, flags);

    if (!scene) {
        printf("Import FAILED: %s\n", importer.GetErrorString());
        printf("\nASSIMP DAE IMPORT = INVALID\n");
        return 1;
    }

    printf("loaded: YES\n");
    printf("scene flags: 0x%X\n", scene->mFlags);
    printf("mesh count: %u\n", scene->mNumMeshes);
    printf("material count: %u\n", scene->mNumMaterials);
    printf("animation count: %u\n", scene->mNumAnimations);
    printf("texture count: %u\n", scene->mNumTextures);

    if (scene->mRootNode) {
        printf("root node: %s\n", scene->mRootNode->mName.C_Str());
        printf("root children: %u\n", scene->mRootNode->mNumChildren);
    }

    unsigned int total_nodes = 0;
    if (scene->mRootNode) countNodes(scene->mRootNode, total_nodes);
    printf("total nodes: %u\n", total_nodes);

    unsigned int total_verts = 0, total_faces = 0, total_indices = 0;
    unsigned int non_tri_faces = 0;
    unsigned int meshes_normals = 0, meshes_uv0 = 0, meshes_colors = 0, meshes_bones = 0;

    for (unsigned int i = 0; i < scene->mNumMeshes; i++) {
        const aiMesh* m = scene->mMeshes[i];
        total_verts += m->mNumVertices;
        total_faces += m->mNumFaces;

        for (unsigned int f = 0; f < m->mNumFaces; f++) {
            total_indices += m->mFaces[f].mNumIndices;
            if (m->mFaces[f].mNumIndices != 3)
                non_tri_faces++;
        }

        if (m->HasNormals()) meshes_normals++;
        if (m->HasTextureCoords(0)) meshes_uv0++;
        if (m->HasVertexColors(0)) meshes_colors++;
        if (m->HasBones()) meshes_bones++;
    }

    printf("total vertices: %u\n", total_verts);
    printf("total faces: %u\n", total_faces);
    printf("total indices: %u\n", total_indices);
    printf("triangle faces: %u\n", total_faces - non_tri_faces);
    printf("non-triangle faces: %u\n", non_tri_faces);
    printf("meshes with normals: %u\n", meshes_normals);
    printf("meshes with UV0: %u\n", meshes_uv0);
    printf("meshes with vertex colors: %u\n", meshes_colors);
    printf("meshes with bones: %u\n", meshes_bones);

    printf("\n=== MATERIALS ===\n");
    for (unsigned int i = 0; i < scene->mNumMaterials && i < 5; i++) {
        const aiMaterial* mat = scene->mMaterials[i];
        aiString name;
        mat->Get(AI_MATKEY_NAME, name);
        printf("  material[%u]: %s", i, name.C_Str());
        aiString texPath;
        if (mat->GetTexture(aiTextureType_DIFFUSE, 0, &texPath) == AI_SUCCESS)
            printf(" [map_Kd: %s]", texPath.C_Str());
        aiColor4D diffuse;
        if (mat->Get(AI_MATKEY_COLOR_DIFFUSE, diffuse) == AI_SUCCESS)
            printf(" [diffuse: %.2f %.2f %.2f]", diffuse.r, diffuse.g, diffuse.b);
        printf("\n");
    }

    printf("\nASSIMP DAE IMPORT = VALID\n");
    return 0;
}
