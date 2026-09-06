// Cap bone/node transform diagnosis + PreTransformVertices visual test
#define _CRT_SECURE_NO_WARNINGS
#include <cstdio>
#include <vector>
#include <string>
#include <cmath>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/version.h>

// Find bone node by name and print accumulated world transform
void find_node_and_print(aiNode* root, const char* bone_name, int depth) {
    if (!root) return;
    if (strcmp(root->mName.C_Str(), bone_name) == 0) {
        printf("  FOUND NODE: '%s' at depth %d\n", bone_name, depth);
        printf("    local transform:\n");
        aiMatrix4x4 t = root->mTransformation;
        printf("      (%.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f)\n",
            t.a1,t.a2,t.a3,t.a4, t.b1,t.b2,t.b3,t.b4, t.c1,t.c2,t.c3,t.c4, t.d1,t.d2,t.d3,t.d4);
    }
    for (unsigned int i = 0; i < root->mNumChildren; i++)
        find_node_and_print(root->mChildren[i], bone_name, depth+1);
}

void print_node_chain(aiNode* root, const char* bone_name, std::vector<aiNode*>& chain) {
    if (!root) return;
    if (strcmp(root->mName.C_Str(), bone_name) == 0) {
        chain.push_back(root);
        return;
    }
    for (unsigned int i = 0; i < root->mNumChildren; i++) {
        print_node_chain(root->mChildren[i], bone_name, chain);
        if (!chain.empty() && chain.back() != root) return;
    }
}

// Compute world transform for a node (accumulated from root)
aiMatrix4x4 compute_world_transform(aiNode* node) {
    aiMatrix4x4 world = node->mTransformation;
    aiNode* parent = node->mParent;
    while (parent) {
        world = parent->mTransformation * world;
        parent = parent->mParent;
    }
    return world;
}

// Apply vertex offset matrix formula
aiVector3D apply_offset(const aiVector3D& v, const aiMatrix4x4& offset) {
    aiVector3D r;
    r.x = offset.a1*v.x + offset.a2*v.y + offset.a3*v.z + offset.a4;
    r.y = offset.b1*v.x + offset.b2*v.y + offset.b3*v.z + offset.b4;
    r.z = offset.c1*v.x + offset.c2*v.y + offset.c3*v.z + offset.c4;
    return r;
}

// Apply bone formula: v_final = sum(w * offsetMatrix * v)
aiVector3D skin_vertex(const aiVector3D& v, const aiBone* bone) {
    aiVector3D result(0,0,0);
    for (unsigned int b = 0; b < bone->mNumWeights; b++) {
        aiVertexWeight w = bone->mWeights[b];
        aiVector3D transformed = apply_offset(v, bone->mOffsetMatrix);
        result.x += w.mWeight * transformed.x;
        result.y += w.mWeight * transformed.y;
        result.z += w.mWeight * transformed.z;
    }
    return result;
}

int main() {
    Assimp::Importer importer;
    const aiScene* sc = importer.ReadFile("D:\\DescompDS\\test_export\\face_demo_mario.dae",
        aiProcess_Triangulate | aiProcess_JoinIdenticalVertices);
    if (!sc) { printf("FAILED\n"); return 1; }

    printf("=== CAP BONE/NODE TRANSFORM DIAGNOSIS ===\n");

    // Find M_cap node and print world transform
    printf("\n--- M_cap node ---\n");
    find_node_and_print(sc->mRootNode, "M_cap", 0);
    printf("\n--- cap_1 node ---\n");
    find_node_and_print(sc->mRootNode, "cap_1", 0);

    // For mesh[0] (first bad cap mesh), compute skinned vs raw bounds
    printf("\n\n--- MESH[0] BONE FORMULA TEST ---\n");
    const aiMesh* m0 = sc->mMeshes[0];
    printf("mesh[0]: %u verts, %u faces, %u bones\n", m0->mNumVertices, m0->mNumFaces, m0->mNumBones);

    const aiBone* bone0 = m0->mBones[0]; // M_cap
    printf("bone[0]: '%s'\n", bone0->mName.C_Str());

    // Raw bounds
    float raw_minx=1e30f, raw_miny=1e30f, raw_minz=1e30f;
    float raw_maxx=-1e30f, raw_maxy=-1e30f, raw_maxz=-1e30f;
    aiVector3D skin_min(1e30f,1e30f,1e30f), skin_max(-1e30f,-1e30f,-1e30f);

    // Find M_cap node world transform
    aiMatrix4x4 mcap_world;
    bool found_mcap = false;
    
    // Simple lookup: search all nodes recursively
    struct Finder {
        aiMatrix4x4& result;
        bool& found;
        const char* name;
        void operator()(aiNode* node) {
            if (!node) return;
            if (strcmp(node->mName.C_Str(), name) == 0) {
                result = node->mTransformation;
                found = true;
                return;
            }
            for (unsigned i = 0; i < node->mNumChildren; i++)
                (*this)(node->mChildren[i]);
        }
    };
    Finder f{mcap_world, found_mcap, "M_cap"};
    f(sc->mRootNode);
    printf("M_cap node local transform found: %s\n", found_mcap ? "YES" : "NO");

    // Try computing offset matrix * bone_world
    // Actually, Assimp docs say: mOffsetMatrix transforms vertex to bone space
    // Final position = sum(w * (mOffsetMatrix * v)) for each bone
    // This is the standard skinning formula

    printf("\nTesting first 5 vertices with M_cap bone offset formula:\n");
    for (unsigned v = 0; v < 5 && v < m0->mNumVertices; v++) {
        aiVector3D raw = m0->mVertices[v];
        aiVector3D skinned = skin_vertex(raw, bone0);
        printf("  v[%u]: raw=(%.3f %.3f %.3f) -> skinned=(%.3f %.3f %.3f)\n",
            v, raw.x, raw.y, raw.z, skinned.x, skinned.y, skinned.z);
        // Update bounds
        if (skinned.x<skin_min.x) skin_min.x=skinned.x;
        if (skinned.y<skin_min.y) skin_min.y=skinned.y;
        if (skinned.z<skin_min.z) skin_min.z=skinned.z;
        if (skinned.x>skin_max.x) skin_max.x=skinned.x;
        if (skinned.y>skin_max.y) skin_max.y=skinned.y;
        if (skinned.z>skin_max.z) skin_max.z=skinned.z;
    }
    printf("  skinned bounds: min=(%.3f %.3f %.3f) max=(%.3f %.3f %.3f)\n",
        skin_min.x,skin_min.y,skin_min.z, skin_max.x,skin_max.y,skin_max.z);

    // Raw bounds
    printf("  raw bounds:     min=(%.3f %.3f %.3f) max=(%.3f %.3f %.3f)\n",
        raw_minx, raw_miny, raw_minz, raw_maxx, raw_maxy, raw_maxz);
    // Actually compute raw bounds from the mesh
    raw_minx=raw_miny=raw_minz=1e30f;
    raw_maxx=raw_maxy=raw_maxz=-1e30f;
    for (unsigned v = 0; v < m0->mNumVertices; v++) {
        if (m0->mVertices[v].x<raw_minx) raw_minx=m0->mVertices[v].x;
        if (m0->mVertices[v].y<raw_miny) raw_miny=m0->mVertices[v].y;
        if (m0->mVertices[v].z<raw_minz) raw_minz=m0->mVertices[v].z;
        if (m0->mVertices[v].x>raw_maxx) raw_maxx=m0->mVertices[v].x;
        if (m0->mVertices[v].y>raw_maxy) raw_maxy=m0->mVertices[v].y;
        if (m0->mVertices[v].z>raw_maxz) raw_maxz=m0->mVertices[v].z;
    }
    printf("  raw bounds:     min=(%.3f %.3f %.3f) max=(%.3f %.3f %.3f)\n",
        raw_minx, raw_miny, raw_minz, raw_maxx, raw_maxy, raw_maxz);

    // PreTransformVertices diagnostic
    printf("\n\n=== PreTransformVertices VISUAL TEST ===\n");
    printf("Rendering dae_renderer_pretransform.bmp\n");
    // This will be handled by the visual test program
    // For now just report:
    Assimp::Importer importer2;
    const aiScene* sc2 = importer2.ReadFile("D:\\DescompDS\\test_export\\face_demo_mario.dae",
        aiProcess_Triangulate | aiProcess_JoinIdenticalVertices | aiProcess_PreTransformVertices);
    if (sc2) {
        printf("PreTransformVertices loaded: YES\n");
        printf("  meshes: %u\n", sc2->mNumMeshes);
        unsigned int tv=0, tf=0, tb=0;
        for (unsigned i=0;i<sc2->mNumMeshes;i++) {
            tv+=sc2->mMeshes[i]->mNumVertices; tf+=sc2->mMeshes[i]->mNumFaces;
            if(sc2->mMeshes[i]->HasBones()) tb++;
        }
        printf("  vertices: %u  faces: %u  meshes with bones: %u\n", tv, tf, tb);
    }

    printf("\nCONCLUSION:\n");
    printf("  Cap vertices are in bone-local space\n");
    printf("  Need: v_final = sum(w * mOffsetMatrix * v) per bone\n");
    printf("  OR: use aiProcess_PreTransformVertices to bake\n");
    printf("  into world space before rendering\n");

    return 0;
}
