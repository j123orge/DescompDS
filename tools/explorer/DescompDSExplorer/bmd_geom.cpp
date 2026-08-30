// DescompDS — Geometry diagnostic: prove geometry sanity + primitive breakdown
#include "bmd.h"
#include "romfile.h"

#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>

int main(int argc, char** argv) {
    if (argc < 3) { std::printf("usage: bmd_geom <rom> <idx>\n"); return 2; }
    std::vector<uint8_t> raw;
    if (!romfile::read_file(argv[1], std::atoi(argv[2]), raw)) { std::printf("read fail\n"); return 1; }
    auto dec = romfile::decompress(raw);
    bmd::Model m; std::string err;
    if (!bmd::parse_model(dec, m, err)) { std::printf("parse fail: %s\n", err.c_str()); return 1; }

    std::printf("file[%s] decompressed=%zu\n", argv[2], dec.size());
    std::printf("groups=%zu bones=%zu\n", m.groups.size(), m.bones.size());

    long verts=0, idx=0, tris=0;
    long per_type[4] = {0,0,0,0};
    bool nan=false, inf=false;
    double minx=1e30,miny=1e30,minz=1e30,maxx=-1e30,maxy=-1e30,maxz=-1e30;
    int ninf = 0;
    for (auto& g : m.groups) for (auto& p : g.prims) {
        per_type[(int)p.type]++;
        verts += (long)p.verts.size();
        if (p.type == bmd::PrimType::Triangles) tris += (int)p.verts.size()/3;
        else if (p.type == bmd::PrimType::TriangleStrip) tris += (int)p.verts.size()-2 >= 0 ? (int)p.verts.size()-2 : 0;
        else if (p.type == bmd::PrimType::Quads) tris += (int)p.verts.size()/2;
        else if (p.type == bmd::PrimType::QuadStrip) tris += (int)p.verts.size()-2 >= 0 ? (int)p.verts.size()-2 : 0;
        for (auto& v : p.verts) {
            if (std::isnan(v.x)||std::isnan(v.y)||std::isnan(v.z)) nan=true;
            if (std::isinf(v.x)||std::isinf(v.y)||std::isinf(v.z)) inf=true;
            double a = std::fabs(v.x)+std::fabs(v.y)+std::fabs(v.z);
            if (a > 1e6) ninf++;
            minx=std::min(minx,(double)v.x); maxx=std::max(maxx,(double)v.x);
            miny=std::min(miny,(double)v.y); maxy=std::max(maxy,(double)v.y);
            minz=std::min(minz,(double)v.z); maxz=std::max(maxz,(double)v.z);
        }
    }
    std::printf("\n=== GEOMETRY REPORT ===\n");
    std::printf("vertex count     : %ld\n", verts);
    std::printf("approx triangles : %ld\n", tris);
    std::printf("primitives by type: tri=%ld quad=%ld tristrip=%ld quadstrip=%ld\n",
                per_type[0], per_type[1], per_type[2], per_type[3]);
    std::printf("RAW bbox         : (%.3f,%.3f,%.3f) - (%.3f,%.3f,%.3f)\n",
                minx,miny,minz,maxx,maxy,maxz);
    std::printf("RAW size         : (%.3f,%.3f,%.3f)\n", maxx-minx, maxy-miny, maxz-minz);
    // WORLD bounds (bone transforms applied) for validation
    {
        double wminx=1e30,wminy=1e30,wminz=1e30,wmaxx=-1e30,wmaxy=-1e30,wmaxz=-1e30;
        long oob_mtx=0, invalid_bone=0;
        for (auto& g : m.groups) for (auto& p : g.prims) for (auto& v : p.verts) {
            int bone_id = 0;
            if (v.matrix_id < 0 || v.matrix_id >= (int)g.bone_ids.size()) oob_mtx++;
            else bone_id = g.bone_ids[v.matrix_id];
            if (bone_id < 0 || bone_id >= (int)m.bones.size()) { invalid_bone++; bone_id = 0; }
            const float* W = m.bones[bone_id].world;
            double x = v.x*W[0]+v.y*W[4]+v.z*W[8]+W[12];
            double y = v.x*W[1]+v.y*W[5]+v.z*W[9]+W[13];
            double z = v.x*W[2]+v.y*W[6]+v.z*W[10]+W[14];
            wminx=std::min(wminx,x); wmaxx=std::max(wmaxx,x);
            wminy=std::min(wminy,y); wmaxy=std::max(wmaxy,y);
            wminz=std::min(wminz,z); wmaxz=std::max(wmaxz,z);
        }
        std::printf("WORLD bbox       : (%.3f,%.3f,%.3f) - (%.3f,%.3f,%.3f)\n", wminx,wminy,wminz,wmaxx,wmaxy,wmaxz);
        std::printf("WORLD size       : (%.3f,%.3f,%.3f)\n", wmaxx-wminx, wmaxy-wminy, wmaxz-wminz);
        std::printf("WORLD center     : (%.3f,%.3f,%.3f)\n", (wminx+wmaxx)/2,(wminy+wmaxy)/2,(wminz+wmaxz)/2);
        std::printf("matrix_id OOB    : %ld   invalid_bone fallback: %ld\n", oob_mtx, invalid_bone);
    }
    double r = 0;
    double cx=(minx+maxx)/2, cy=(miny+maxy)/2, cz=(minz+maxz)/2;
    for (auto& g : m.groups) for (auto& p : g.prims) for (auto& v : p.verts) {
        double dx=v.x-cx, dy=v.y-cy, dz=v.z-cz;
        r=std::max(r, std::sqrt(dx*dx+dy*dy+dz*dz));
    }
    std::printf("approx radius    : %.3f\n", r);
    std::printf("NaN present      : %s\n", nan?"YES":"no");
    std::printf("INF present      : %s\n", inf?"YES":"no");
    std::printf("coords abs>1e6   : %d\n", ninf);
    return 0;
}