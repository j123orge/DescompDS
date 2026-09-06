// Assimp DAE comprehensive diagnostic — OFFICIAL CMake target assimp::assimp
// Tests: scene data, node hierarchy, bone info, static pose
#define _CRT_SECURE_NO_WARNINGS
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <functional>
#include <cfloat>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/version.h>

// ---- helpers ----
struct Bounds { float min[3], max[3]; void reset() { for(int i=0;i<3;i++){min[i]=1e30f;max[i]=-1e30f;} } };
void expand(Bounds& b, const aiVector3D& v) {
    if(v.x<b.min[0]) b.min[0]=v.x; if(v.x>b.max[0]) b.max[0]=v.x;
    if(v.y<b.min[1]) b.min[1]=v.y; if(v.y>b.max[1]) b.max[1]=v.y;
    if(v.z<b.min[2]) b.min[2]=v.z; if(v.z>b.max[2]) b.max[2]=v.z;
}
void expandMat(Bounds& b, const aiMesh* m, const aiMatrix4x4& w) {
    for(unsigned int i=0;i<m->mNumVertices;i++){
        aiVector3D v=m->mVertices[i];
        aiVector3D r = w * v;
        expand(b,r);
    }
}
void printBounds(const char* label, const Bounds& b) {
    printf("  %s: min=(%.4f %.4f %.4f) max=(%.4f %.4f %.4f)\n",
        label, b.min[0],b.min[1],b.min[2], b.max[0],b.max[1],b.max[2]);
}

// ---- main ----
int main(int argc, char** argv) {
    const char* dae_path = argc > 1 ? argv[1] : "D:\\DescompDS\\test_export\\face_demo_mario.dae";

    printf("=== ASSIMP DAE COMPREHENSIVE DIAGNOSTIC ===\n\n");

    // ---- ASSIMP BUILD ----
    printf("ASSIMP BUILD:\n");
    printf("  version: %u.%u.%u\n", aiGetVersionMajor(), aiGetVersionMinor(), aiGetVersionPatch());
    printf("  CMake target: assimp::assimp (add_subdirectory)\n");
    printf("  source path: D:/DescompDS/deps/assimp/assimp-6.0.5\n");

    // ---- IMPORT ----
    Assimp::Importer importer;
    unsigned int flags = aiProcess_Triangulate | aiProcess_JoinIdenticalVertices;
    const aiScene* scene = importer.ReadFile(dae_path, flags);

    printf("\nASSIMP SCENE:\n");
    if (!scene) {
        printf("  loaded: NO\n");
        printf("  error: %s\n", importer.GetErrorString());
        printf("\nASSIMP DAE IMPORT = INVALID\n");
        return 1;
    }
    printf("  loaded: YES\n");
    printf("  scene flags: 0x%X\n", scene->mFlags);
    printf("  meshes: %u\n", scene->mNumMeshes);
    printf("  materials: %u\n", scene->mNumMaterials);
    printf("  animations: %u\n", scene->mNumAnimations);
    printf("  textures: %u\n", scene->mNumTextures);
    printf("  root node: %s\n", scene->mRootNode ? scene->mRootNode->mName.C_Str() : "(null)");

    // ---- NODE HIERARCHY ----
    unsigned int total_nodes = 0, nodes_with_meshes = 0, total_mesh_refs = 0;
    std::function<void(aiNode*)> traverse = [&](aiNode* n) {
        total_nodes++;
        if (n->mNumMeshes > 0) { nodes_with_meshes++; total_mesh_refs += n->mNumMeshes; }
        for (unsigned int i = 0; i < n->mNumChildren; i++) traverse(n->mChildren[i]);
    };
    if (scene->mRootNode) traverse(scene->mRootNode);
    printf("  nodes: %u\n", total_nodes);
    printf("  nodes referencing meshes: %u\n", nodes_with_meshes);
    printf("  total mesh references: %u\n", total_mesh_refs);

    // ---- MESH TOTALS ----
    unsigned int total_verts=0, total_faces=0, total_indices=0, non_tri=0;
    unsigned int m_normals=0, m_uv0=0, m_colors=0, m_bones=0;
    unsigned int total_bones=0, total_weights=0;

    for (unsigned int i = 0; i < scene->mNumMeshes; i++) {
        const aiMesh* m = scene->mMeshes[i];
        total_verts += m->mNumVertices;
        total_faces += m->mNumFaces;
        for (unsigned int f = 0; f < m->mNumFaces; f++) {
            total_indices += m->mFaces[f].mNumIndices;
            if (m->mFaces[f].mNumIndices != 3) non_tri++;
        }
        if (m->HasNormals()) m_normals++;
        if (m->HasTextureCoords(0)) m_uv0++;
        if (m->HasVertexColors(0)) m_colors++;
        if (m->HasBones()) { m_bones++; total_bones += m->mNumBones; for(unsigned int b=0;b<m->mNumBones;b++) total_weights += m->mBones[b]->mNumWeights; }
    }

    printf("\nMESH TOTALS:\n");
    printf("  vertices: %u\n", total_verts);
    printf("  faces: %u\n", total_faces);
    printf("  indices: %u\n", total_indices);
    printf("  triangle faces: %u\n", total_faces - non_tri);
    printf("  non-triangle faces: %u\n", non_tri);
    printf("  meshes with normals: %u/%u\n", m_normals, scene->mNumMeshes);
    printf("  meshes with UV0: %u/%u\n", m_uv0, scene->mNumMeshes);
    printf("  meshes with colors0: %u/%u\n", m_colors, scene->mNumMeshes);
    printf("  meshes with bones: %u/%u\n", m_bones, scene->mNumMeshes);
    printf("  total bones: %u\n", total_bones);
    printf("  total weights: %u\n", total_weights);

    // ---- MATERIALS ----
    printf("\nMATERIALS:\n");
    unsigned int textured = 0;
    for (unsigned int i = 0; i < scene->mNumMaterials; i++) {
        const aiMaterial* mat = scene->mMaterials[i];
        aiString name; mat->Get(AI_MATKEY_NAME, name);
        aiString texPath; bool hasTex = mat->GetTexture(aiTextureType_DIFFUSE, 0, &texPath) == AI_SUCCESS;
        aiColor4D diff; bool hasDiff = mat->Get(AI_MATKEY_COLOR_DIFFUSE, diff) == AI_SUCCESS;
        if (hasTex) textured++;
        printf("  [%u] %s", i, name.C_Str());
        if (hasTex) printf(" [tex: %s]", texPath.C_Str());
        if (hasDiff) printf(" [diffuse: %.2f %.2f %.2f]", diff.r, diff.g, diff.b);
        printf("\n");
    }
    printf("  textured materials: %u/%u\n", textured, scene->mNumMaterials);

    // ---- BONE DETAILS ----
    printf("\nBONE DETAILS:\n");
    for (unsigned int i = 0; i < scene->mNumMeshes; i++) {
        const aiMesh* m = scene->mMeshes[i];
        if (!m->HasBones()) continue;
        printf("  mesh[%u]: %u bones, %u weights\n", i, m->mNumBones, (unsigned)([&]{ unsigned w=0; for(unsigned b=0;b<m->mNumBones;b++) w+=m->mBones[b]->mNumWeights; return w; }()));
        for (unsigned int b = 0; b < m->mNumBones && b < 5; b++) {
            printf("    bone[%u]: \"%s\" weights=%u\n", b, m->mBones[b]->mName.C_Str(), m->mBones[b]->mNumWeights);
            // Check weight sum for first vertex
            if (m->mBones[b]->mNumWeights > 0) {
                aiVertexWeight w0 = m->mBones[b]->mWeights[0];
                printf("      first weight: vertex=%u weight=%.4f\n", w0.mVertexId, w0.mWeight);
            }
        }
    }

    // ---- TEST A: RAW MESH BOUNDS (no transforms) ----
    printf("\nTRANSFORM TEST:\n");
    Bounds raw; raw.reset();
    for (unsigned int i = 0; i < scene->mNumMeshes; i++) {
        const aiMesh* m = scene->mMeshes[i];
        for (unsigned int v = 0; v < m->mNumVertices; v++)
            expand(raw, m->mVertices[v]);
    }
    printBounds("raw bounds (no transforms)", raw);

    // ---- TEST B: NODE-TRANSFORMED BOUNDS ----
    Bounds xformed; xformed.reset();
    std::function<void(aiNode*, aiMatrix4x4)> xformTraverse = [&](aiNode* n, aiMatrix4x4 parent) {
        aiMatrix4x4 world = parent * n->mTransformation;
        for (unsigned int mi = 0; mi < n->mNumMeshes; mi++) {
            const aiMesh* m = scene->mMeshes[n->mMeshes[mi]];
            expandMat(xformed, m, world);
        }
        for (unsigned int i = 0; i < n->mNumChildren; i++)
            xformTraverse(n->mChildren[i], world);
    };
    aiMatrix4x4 identity; // default constructor = identity
    if (scene->mRootNode) xformTraverse(scene->mRootNode, identity);
    printBounds("node-transformed bounds", xformed);

    // Compare
    bool bounds_differ = false;
    for (int i = 0; i < 3; i++) {
        if (fabsf(raw.min[i]-xformed.min[i])>0.001f || fabsf(raw.max[i]-xformed.max[i])>0.001f)
            bounds_differ = true;
    }
    printf("  bounds differ after node transforms: %s\n", bounds_differ ? "YES" : "NO");

    // ---- TEST C: PreTransformVertices diagnostic ----
    printf("\nPreTransformVertices DIAGNOSTIC:\n");
    Assimp::Importer importer2;
    unsigned int flags2 = aiProcess_Triangulate | aiProcess_JoinIdenticalVertices | aiProcess_PreTransformVertices;
    const aiScene* scene2 = importer2.ReadFile(dae_path, flags2);
    if (scene2) {
        printf("  loaded: YES\n");
        printf("  meshes: %u\n", scene2->mNumMeshes);
        unsigned int v2=0,f2=0;
        for (unsigned int i=0;i<scene2->mNumMeshes;i++) { v2+=scene2->mMeshes[i]->mNumVertices; f2+=scene2->mMeshes[i]->mNumFaces; }
        printf("  vertices: %u\n", v2);
        printf("  faces: %u\n", f2);
        unsigned int b2=0;
        for (unsigned int i=0;i<scene2->mNumMeshes;i++) if(scene2->mMeshes[i]->HasBones()) b2++;
        printf("  meshes with bones: %u\n", b2);
        Bounds bpt; bpt.reset();
        for (unsigned int i=0;i<scene2->mNumMeshes;i++)
            for (unsigned int v=0;v<scene2->mMeshes[i]->mNumVertices;v++)
                expand(bpt, scene2->mMeshes[i]->mVertices[v]);
        printBounds("pretransform bounds", bpt);
    } else {
        printf("  loaded: NO (%s)\n", importer2.GetErrorString());
    }

    printf("\nASSIMP DAE IMPORT = VALID\n");
    return 0;
}
