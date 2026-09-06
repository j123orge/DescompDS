// DescompDS Explorer — Assimp → NeutralMesh adapter implementation
#define _CRT_SECURE_NO_WARNINGS
#include "assimp_loader.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <cstring>
#include <functional>

static void expand_bounds(float min[3], float max[3], const aiVector3D& v) {
    if (v.x < min[0]) min[0] = v.x; if (v.x > max[0]) max[0] = v.x;
    if (v.y < min[1]) min[1] = v.y; if (v.y > max[1]) max[1] = v.y;
    if (v.z < min[2]) min[2] = v.z; if (v.z > max[2]) max[2] = v.z;
}

static aiMatrix4x4 identity4x4() {
    aiMatrix4x4 m;
    return m; // default ctor = identity
}

static void traverse_and_load(
    aiNode* node,
    const aiMatrix4x4& parent_world,
    const aiScene* scene,
    NeutralMesh& out,
    uint32_t& base_vertex
) {
    aiMatrix4x4 world = parent_world * node->mTransformation;

    for (unsigned int mi = 0; mi < node->mNumMeshes; mi++) {
        const aiMesh* mesh = scene->mMeshes[node->mMeshes[mi]];
        unsigned int mat_idx = mesh->mMaterialIndex;

        uint32_t mesh_start_idx = (uint32_t)out.indices.size();
        uint32_t mesh_vertex_start = base_vertex;

        for (unsigned int vi = 0; vi < mesh->mNumVertices; vi++) {
            NeutralVertex v = {};

            // position: bake world transform
            aiVector3D pos = world * mesh->mVertices[vi];
            v.pos[0] = pos.x; v.pos[1] = pos.y; v.pos[2] = pos.z;

            // normal: bake rotation part only (ignore translation)
            if (mesh->HasNormals()) {
                aiMatrix3x3 rot(world);
                aiVector3D n = (rot * mesh->mNormals[vi]).Normalize();
                v.normal[0] = n.x; v.normal[1] = n.y; v.normal[2] = n.z;
            }

            // UV
            if (mesh->HasTextureCoords(0)) {
                v.uv[0] = mesh->mTextureCoords[0][vi].x;
                v.uv[1] = mesh->mTextureCoords[0][vi].y;
            }

            // vertex color
            if (mesh->HasVertexColors(0)) {
                v.color[0] = (uint8_t)(mesh->mColors[0][vi].r * 255.0f);
                v.color[1] = (uint8_t)(mesh->mColors[0][vi].g * 255.0f);
                v.color[2] = (uint8_t)(mesh->mColors[0][vi].b * 255.0f);
                v.color[3] = (uint8_t)(mesh->mColors[0][vi].a * 255.0f);
            } else {
                v.color[0] = v.color[1] = v.color[2] = v.color[3] = 255;
            }

            out.vertices.push_back(v);
            expand_bounds(out.bbox_min, out.bbox_max, pos);
            base_vertex++;
        }

        // faces (all triangles after aiProcess_Triangulate)
        for (unsigned int fi = 0; fi < mesh->mNumFaces; fi++) {
            const aiFace& face = mesh->mFaces[fi];
            for (unsigned int ii = 0; ii < face.mNumIndices; ii++) {
                out.indices.push_back(mesh_vertex_start + face.mIndices[ii]);
            }
        }

        // submesh
        NeutralSubmesh sm;
        sm.index_start = mesh_start_idx;
        sm.index_count = (uint32_t)out.indices.size() - mesh_start_idx;
        sm.material_id = (int)mat_idx;
        out.submeshes.push_back(sm);
    }

    for (unsigned int ci = 0; ci < node->mNumChildren; ci++) {
        traverse_and_load(node->mChildren[ci], world, scene, out, base_vertex);
    }
}

bool load_dae_to_neutral(const char* dae_path, NeutralMesh& out) {
    Assimp::Importer importer;
    unsigned int flags = aiProcess_Triangulate | aiProcess_JoinIdenticalVertices | aiProcess_PreTransformVertices;
    const aiScene* scene = importer.ReadFile(dae_path, flags);
    if (!scene || !scene->mRootNode) return false;

    out.clear();

    // materials
    for (unsigned int i = 0; i < scene->mNumMaterials; i++) {
        const aiMaterial* mat = scene->mMaterials[i];
        NeutralMaterial nm;

        aiString name;
        if (mat->Get(AI_MATKEY_NAME, name) == AI_SUCCESS)
            nm.name = name.C_Str();

        aiColor4D diff;
        if (mat->Get(AI_MATKEY_COLOR_DIFFUSE, diff) == AI_SUCCESS) {
            nm.diffuse[0] = diff.r; nm.diffuse[1] = diff.g; nm.diffuse[2] = diff.b;
        } else {
            nm.diffuse[0] = nm.diffuse[1] = nm.diffuse[2] = 1.0f;
        }

        aiString texPath;
        if (mat->GetTexture(aiTextureType_DIFFUSE, 0, &texPath) == AI_SUCCESS)
            nm.diffuse_tex = texPath.C_Str();

        out.materials.push_back(nm);
    }

    // collect texture filenames
    for (auto& m : out.materials) {
        if (!m.diffuse_tex.empty())
            out.texture_files.push_back(m.diffuse_tex);
    }

    // traverse hierarchy and build geometry
    uint32_t base_vertex = 0;
    traverse_and_load(scene->mRootNode, identity4x4(), scene, out, base_vertex);

    return !out.vertices.empty();
}
