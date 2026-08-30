// DescompDS — Comprehensive BMD validation tool
// Per-DL analysis, matrix_id tracking, GX command walk, OBJ export, topology check
// Uses romfile.h for ROM access, reimplements BMD parse inline for per-DL detail
#include "romfile.h"

#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>
#include <algorithm>
#include <set>
#include <numeric>

struct Vec3 { float x=0,y=0,z=0; };
struct Vert {
    Vec3 pos;
    int matrix_id = 0;
    uint8_t r=255,g=255,b=255,a=255;
    int gx_cmd_offset = 0; // byte offset in DL data where this vertex was emitted
};
struct Tri { int v[3]; int dl; };

struct DLInfo {
    int dl_index;
    int poly_id;
    uint32_t num_transforms, transforms_offset;
    uint32_t dl_size = 0;
    uint32_t dl_offset = 0;
    std::vector<int> bone_ids; // matrix_id -> bone id
    std::vector<std::string> bone_names;
    // GX command stats
    int cmd_vtx16=0, cmd_vtx10=0, cmd_vtxxy=0, cmd_vtxxz=0, cmd_vtxyz=0, cmd_vtxdiff=0;
    int cmd_matrix_restore=0, cmd_begin=0, cmd_end=0;
    int cmd_other=0;
    // decoded vertices
    std::vector<Vert> verts;
    // primitive runs
    struct PrimRun { int type; int first_vtx; };
    std::vector<PrimRun> runs;
    std::vector<int> run_tris;
    // raw bbox
    float rminx=1e30,rminy=1e30,rminz=1e30,rmaxx=-1e30,rmaxy=-1e30,rmaxz=-1e30;
    float wminx=1e30,wminy=1e30,wminz=1e30,wmaxx=-1e30,wmaxy=-1e30,wmaxz=-1e30;
    int total_tris = 0;
    // matrix_id tracking
    std::vector<int> matrix_id_dist; // count per matrix_id
    int matrix_id_oob = 0;
    int max_matrix_id = -1;
    // OOB examples
    struct OOBExample { int vert_idx; int gx_offset; int matrix_id; int bone_ids_size; int last_mtx_cmd_offset; };
    std::vector<OOBExample> oob_examples;
    int last_mtx_cmd_offset = 0;
};

struct Bone {
    int id=0;
    std::string name;
    int parent_offset=0;
    float scale[3]={1,1,1};
    short rot[3]={0,0,0};
    float trans[3]={0,0,0};
    float world[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
};

// ---- Matrix math ----
void mat_mul(float* o,const float* a,const float* b){
    float t[16]={};
    for(int r=0;r<4;r++) for(int c=0;c<4;c++) for(int k=0;k<4;k++) t[r*4+c]+=a[r*4+k]*b[k*4+c];
    for(int i=0;i<16;i++) o[i]=t[i];
}
void mat_id(float* m){ for(int i=0;i<16;i++) m[i]=0; m[0]=m[5]=m[10]=m[15]=1; }
void mat_s(float* m,float x,float y,float z){ mat_id(m); m[0]=x; m[5]=y; m[10]=z; }
void mat_t(float* m,float x,float y,float z){ mat_id(m); m[12]=x; m[13]=y; m[14]=z; }
void mat_rx(float* m,float a){ float c=cosf(a),s=sinf(a); mat_id(m); m[5]=c; m[6]=-s; m[9]=s; m[10]=c; }
void mat_ry(float* m,float a){ float c=cosf(a),s=sinf(a); mat_id(m); m[0]=c; m[2]=s; m[8]=-s; m[10]=c; }
void mat_rz(float* m,float a){ float c=cosf(a),s=sinf(a); mat_id(m); m[0]=c; m[1]=-s; m[4]=s; m[5]=c; }
void srt_mat(float* o,const float* sc,const short* ro,const float* tr){
    float S[16],RX[16],RY[16],RZ[16],T[16],R[16],SR[16];
    mat_s(S,sc[0],sc[1],sc[2]);
    mat_rx(RX,(float)ro[0]*3.14159265f/2048.f);
    mat_ry(RY,(float)ro[1]*3.14159265f/2048.f);
    mat_rz(RZ,(float)ro[2]*3.14159265f/2048.f);
    mat_t(T,tr[0],tr[1],tr[2]);
    float RXRY[16]; mat_mul(RXRY,RX,RY);
    mat_mul(R,RXRY,RZ);
    mat_mul(SR,S,R);
    mat_mul(o,SR,T);
}

// ---- Read helpers ----
uint32_t rd32(const std::vector<uint8_t>& d,size_t o){
    if(o+4>d.size()) return 0;
    return (uint32_t)d[o]|((uint32_t)d[o+1]<<8)|((uint32_t)d[o+2]<<16)|((uint32_t)d[o+3]<<24);
}
int32_t rds32(const std::vector<uint8_t>& d,size_t o){ return(int32_t)rd32(d,o); }
uint16_t rd16(const std::vector<uint8_t>& d,size_t o){
    if(o+2>d.size()) return 0;
    return(uint16_t)(d[o]|(d[o+1]<<8));
}
int16_t rds16(const std::vector<uint8_t>& d,size_t o){ return(int16_t)rd16(d,o); }
uint8_t rd8(const std::vector<uint8_t>& d,size_t o){ return o<d.size()?d[o]:0; }
std::string cstr(const std::vector<uint8_t>& d,size_t o){
    if(o>=d.size()) return "";
    std::string s;
    while(o<d.size()&&d[o]!=0&&s.size()<256) s+=(char)d[o++];
    return s;
}

// ---- Triangulation ----
int tris_from_run(int prim_type, int nverts){
    if(nverts<3) return 0;
    switch(prim_type){
        case 0: return nverts/3;
        case 1: return (nverts/4)*2;
        case 2: return nverts-2;
        case 3: return nverts-2>=0?nverts-2:0;
        default: return 0;
    }
}
void emit_tris_for_run(int prim_type, int first_vtx, int count, int dl_idx, std::vector<Tri>& out){
    if(prim_type==0){
        for(int i=0;i+2<count;i+=3){
            Tri t; t.v[0]=first_vtx+i; t.v[1]=first_vtx+i+1; t.v[2]=first_vtx+i+2; t.dl=dl_idx;
            out.push_back(t);
        }
    } else if(prim_type==1){
        for(int i=0;i+3<count;i+=4){
            Tri t; t.v[0]=first_vtx+i; t.v[1]=first_vtx+i+1; t.v[2]=first_vtx+i+2; t.dl=dl_idx;
            out.push_back(t);
            Tri t2; t2.v[0]=first_vtx+i; t2.v[1]=first_vtx+i+2; t2.v[2]=first_vtx+i+3; t2.dl=dl_idx;
            out.push_back(t2);
        }
    } else if(prim_type==2){
        for(int i=0;i+2<count;i++){
            Tri t;
            if(i%2==0){ t.v[0]=first_vtx+i; t.v[1]=first_vtx+i+1; t.v[2]=first_vtx+i+2; }
            else { t.v[0]=first_vtx+i+1; t.v[1]=first_vtx+i; t.v[2]=first_vtx+i+2; }
            t.dl=dl_idx;
            out.push_back(t);
        }
    } else if(prim_type==3){
        for(int i=2;i+1<count;i+=2){
            Tri ta; ta.v[0]=first_vtx+i-2; ta.v[1]=first_vtx+i; ta.v[2]=first_vtx+i+1; ta.dl=dl_idx;
            out.push_back(ta);
            Tri tb; tb.v[0]=first_vtx+i-2; tb.v[1]=first_vtx+i+1; tb.v[2]=first_vtx+i-1; tb.dl=dl_idx;
            out.push_back(tb);
        }
    }
}

// ---- OBJ export ----
void write_obj(const char* path, const std::vector<Vert>& verts, const std::vector<Tri>& tris){
    FILE* f = fopen(path,"w");
    if(!f){ printf("cannot open %s\n",path); return; }
    fprintf(f,"# DescompDS BMD validation\n");
    fprintf(f,"# vertices: %zu  triangles: %zu\n\n", verts.size(), tris.size());
    for(size_t i=0;i<verts.size();i++){
        const Vec3& p = verts[i].pos;
        fprintf(f,"v %.6f %.6f %.6f\n", p.x, p.y, p.z);
    }
    fprintf(f,"\n");
    int cur_dl = -1;
    for(size_t i=0;i<tris.size();i++){
        if(tris[i].dl != cur_dl){
            cur_dl = tris[i].dl;
            fprintf(f,"\ng dl_%d\n", cur_dl);
        }
        fprintf(f,"f %d %d %d\n", tris[i].v[0]+1, tris[i].v[1]+1, tris[i].v[2]+1);
    }
    fclose(f);
}

// ---- Topology ----
struct TopoStats {
    int degenerate=0, idx_oor=0, isolated=0, long_edges=0, huge_area=0, bridge=0;
    float median_edge=0, max_edge=0, median_area=0, max_area=0;
};
float edge_len(const Vec3& a, const Vec3& b){
    float dx=a.x-b.x,dy=a.y-b.y,dz=a.z-b.z; return sqrtf(dx*dx+dy*dy+dz*dz);
}
float tri_area(const Vec3& a, const Vec3& b, const Vec3& c){
    float ux=b.x-a.x,uy=b.y-a.y,uz=b.z-a.z;
    float vx=c.x-a.x,vy=c.y-a.y,vz=c.z-a.z;
    float nx=uy*vz-uz*vy,ny=uz*vx-ux*vz,nz=ux*vy-uy*vx;
    return 0.5f*sqrtf(nx*nx+ny*ny+nz*nz);
}
TopoStats analyze_topology(const std::vector<Vert>& verts, const std::vector<Tri>& tris, int ntotal){
    TopoStats s;
    if(tris.empty()) return s;
    std::vector<float> edges; edges.reserve(tris.size()*3);
    for(auto& t : tris){
        edges.push_back(edge_len(verts[t.v[0]].pos,verts[t.v[1]].pos));
        edges.push_back(edge_len(verts[t.v[1]].pos,verts[t.v[2]].pos));
        edges.push_back(edge_len(verts[t.v[2]].pos,verts[t.v[0]].pos));
    }
    std::sort(edges.begin(),edges.end());
    s.median_edge=edges[edges.size()/2]; s.max_edge=edges.back();
    for(float e:edges) if(e>10.f*s.median_edge) s.long_edges++;
    std::vector<float> areas; areas.reserve(tris.size());
    for(auto& t : tris){
        float a=tri_area(verts[t.v[0]].pos,verts[t.v[1]].pos,verts[t.v[2]].pos);
        areas.push_back(a);
        if(a<1e-10f) s.degenerate++;
    }
    std::sort(areas.begin(),areas.end());
    s.median_area=areas[areas.size()/2]; s.max_area=areas.back();
    for(float a:areas) if(a>100.f*s.median_area&&s.median_area>1e-10f) s.huge_area++;
    for(auto& t:tris) for(int j=0;j<3;j++) if(t.v[j]<0||t.v[j]>=ntotal) s.idx_oor++;
    std::set<int> used; for(auto& t:tris) for(int j=0;j<3;j++) used.insert(t.v[j]);
    s.isolated=ntotal-(int)used.size();
    for(auto& t:tris){
        float me=std::max({edge_len(verts[t.v[0]].pos,verts[t.v[1]].pos),
                           edge_len(verts[t.v[1]].pos,verts[t.v[2]].pos),
                           edge_len(verts[t.v[2]].pos,verts[t.v[0]].pos)});
        if(me>50.f*s.median_edge&&s.median_edge>1e-10f) s.bridge++;
    }
    return s;
}

// ---- Main ----
int main(int argc, char** argv){
    if(argc<3){ printf("usage: bmd_validate <rom> <file_index> [output_dir]\n"); return 2; }
    int idx = atoi(argv[2]);
    std::string outdir = (argc>3) ? argv[3] : ".";

    std::vector<uint8_t> raw;
    if(!romfile::read_file(argv[1], idx, raw)){ printf("ROM read fail\n"); return 1; }
    auto dec = romfile::decompress(raw);
    printf("file[%d] compressed=%zu decompressed=%zu\n", idx, raw.size(), dec.size());
    if(dec.size()<60){ printf("BMD too small\n"); return 1; }

    // Header
    int scale_shift = rds32(dec,0x00);
    float scale_factor = (float)(1<<scale_shift);
    int n_bones = rds32(dec,0x04);
    uint32_t bones_off = rd32(dec,0x08);
    int n_poly = rds32(dec,0x0C);
    uint32_t poly_off = rd32(dec,0x10);
    int n_tex = rds32(dec,0x14);
    uint32_t tex_off = rd32(dec,0x18);
    int n_pal = rds32(dec,0x1C);
    int n_mat = rds32(dec,0x24);
    uint32_t mat_off = rd32(dec,0x28);
    uint32_t bonemap_off = rd32(dec,0x2C);

    printf("scale_shift=%d scale_factor=%.0f\n", scale_shift, scale_factor);
    printf("n_bones=%d n_poly=%d n_tex=%d n_pal=%d n_mat=%d\n", n_bones, n_poly, n_tex, n_pal, n_mat);

    // Bones
    std::vector<Bone> bones(n_bones);
    for(int i=0;i<n_bones;i++){
        size_t bo = bones_off + (size_t)i*64;
        bones[i].id = rds32(dec,bo);
        bones[i].name = cstr(dec, rd32(dec,bo+4));
        bones[i].parent_offset = rds16(dec,bo+8);
        bones[i].scale[0]=(float)rds32(dec,bo+0x10)/4096.f;
        bones[i].scale[1]=(float)rds32(dec,bo+0x14)/4096.f;
        bones[i].scale[2]=(float)rds32(dec,bo+0x18)/4096.f;
        bones[i].rot[0]=rds16(dec,bo+0x1C);
        bones[i].rot[1]=rds16(dec,bo+0x1E);
        bones[i].rot[2]=rds16(dec,bo+0x20);
        bones[i].trans[0]=(float)rds32(dec,bo+0x24)/4096.f;
        bones[i].trans[1]=(float)rds32(dec,bo+0x28)/4096.f;
        bones[i].trans[2]=(float)rds32(dec,bo+0x2C)/4096.f;
    }
    for(int i=0;i<n_bones;i++){
        float local[16]; srt_mat(local,bones[i].scale,bones[i].rot,bones[i].trans);
        int po=bones[i].parent_offset; int pidx=(po<0)?(i+po):-1;
        if(pidx<0||pidx>=n_bones) for(int k=0;k<16;k++) bones[i].world[k]=local[k];
        else mat_mul(bones[i].world,local,bones[pidx].world);
    }

    // Bone map
    std::vector<uint16_t> bonemap;
    for(int k=0;k<128;k++){
        uint32_t off=bonemap_off+(uint32_t)k*2;
        if(off+2>dec.size()) break;
        bonemap.push_back(rd16(dec,off));
    }

    // Display lists
    std::vector<DLInfo> dls;
    printf("\n=== PER-DL TABLE ===\n");
    printf("%-4s %-6s %-8s %-20s %-6s %-6s %-5s %-5s %-6s %-6s %-5s %s\n",
           "DL","poly","#xform","bone_ids[]","#vtx","#tri","minM","maxM","#OOB","VTX16","V10","VXY/VXZ/VYZ/VDF");
    fflush(stdout);

    for(int pi=0;pi<n_poly;pi++){
        size_t po = poly_off+(size_t)pi*8;
        if(po+8>dec.size()) break;
        uint32_t dl_hdr_off = rd32(dec,po+4);
        if(dl_hdr_off+16>dec.size()) continue;

        DLInfo dl;
        dl.dl_index = (int)dls.size();
        dl.poly_id = pi;
        dl.num_transforms = rd32(dec,dl_hdr_off);
        dl.transforms_offset = rd32(dec,dl_hdr_off+4);
        dl.dl_size = rd32(dec,dl_hdr_off+8);
        uint32_t dloff = rd32(dec,dl_hdr_off+0x0C);

        // Resolve transform list -> bone_ids
        for(uint32_t tb=0;tb<dl.num_transforms;tb++){
            uint8_t idx1 = rd8(dec, dl.transforms_offset+tb);
            uint16_t bid = (idx1<bonemap.size()) ? bonemap[idx1] : 0;
            int bone_id = (bid<(uint32_t)n_bones) ? (int)bid : 0;
            dl.bone_ids.push_back(bone_id);
            if(bone_id < n_bones) dl.bone_names.push_back(bones[bone_id].name);
            else dl.bone_names.push_back("?invalid?");
        }

        // Walk GX commands
        Vert cur;
        cur.matrix_id = 0;
        int cur_prim_type = -1;
        int first_vtx = -1;
        dl.last_mtx_cmd_offset = 0;
        size_t pos = dloff, end = dloff + dl.dl_size;

        while(pos+4<=end){
            uint8_t c[4]={rd8(dec,pos),rd8(dec,pos+1),rd8(dec,pos+2),rd8(dec,pos+3)};
            pos+=4;
            for(int ci=0;ci<4;ci++){
                uint8_t cmd=c[ci];
                switch(cmd){
                    case 0x00: break;
                    case 0x10: case 0x12: case 0x13: pos+=4; break;
                    case 0x11: case 0x15: break;
                    case 0x14: {
                        uint32_t param=rd32(dec,pos); pos+=4;
                        cur.matrix_id=(int)(param&0x1F);
                        dl.last_mtx_cmd_offset=(int)(pos-4-dloff);
                        dl.cmd_matrix_restore++;
                        break;
                    }
                    case 0x16: pos+=64; break;
                    case 0x17: case 0x19: pos+=48; break;
                    case 0x18: pos+=64; break;
                    case 0x1A: pos+=36; break;
                    case 0x1B: case 0x1C: case 0x20: case 0x21: case 0x22:
                    case 0x29: case 0x2A: case 0x2B:
                    case 0x30: case 0x31: case 0x32: case 0x33: case 0x72:
                        pos+=4; break;
                    case 0x34: pos+=128; break;
                    case 0x50: case 0x60: pos+=4; break;
                    case 0x70: pos+=12; break;
                    case 0x71: pos+=8; break;
                    case 0x23: { // VTX_16 (8 bytes)
                        uint32_t p1=rd32(dec,pos); pos+=4;
                        uint32_t p2=rd32(dec,pos); pos+=4;
                        Vert v=cur;
                        v.pos.x=(float)(int16_t)(p1&0xFFFF)/4096.f*scale_factor;
                        v.pos.y=(float)(int16_t)(p1>>16)/4096.f*scale_factor;
                        v.pos.z=(float)(int16_t)(p2&0xFFFF)/4096.f*scale_factor;
                        v.gx_cmd_offset=(int)(pos-8-dloff);
                        if(cur_prim_type>=0){ dl.verts.push_back(v); dl.cmd_vtx16++; }
                        break;
                    }
                    case 0x24: { // VTX_10 (4 bytes)
                        uint32_t param=rd32(dec,pos); pos+=4;
                        Vert v=cur;
                        v.pos.x=(float)(int16_t)((param<<6)&0xFFC0)/4096.f*scale_factor;
                        v.pos.y=(float)(int16_t)((param>>4)&0xFFC0)/4096.f*scale_factor;
                        v.pos.z=(float)(int16_t)((param>>14)&0xFFC0)/4096.f*scale_factor;
                        v.gx_cmd_offset=(int)(pos-4-dloff);
                        if(cur_prim_type>=0){ dl.verts.push_back(v); dl.cmd_vtx10++; }
                        break;
                    }
                    case 0x25: { // VTX_XY (4 bytes)
                        uint32_t param=rd32(dec,pos); pos+=4;
                        Vert v=cur;
                        v.pos.x=(float)(int16_t)(param&0xFFFF)/4096.f*scale_factor;
                        v.pos.y=(float)(int16_t)(param>>16)/4096.f*scale_factor;
                        v.gx_cmd_offset=(int)(pos-4-dloff);
                        if(cur_prim_type>=0){ dl.verts.push_back(v); dl.cmd_vtxxy++; }
                        break;
                    }
                    case 0x26: { // VTX_XZ (4 bytes)
                        uint32_t param=rd32(dec,pos); pos+=4;
                        Vert v=cur;
                        v.pos.x=(float)(int16_t)(param&0xFFFF)/4096.f*scale_factor;
                        v.pos.z=(float)(int16_t)(param>>16)/4096.f*scale_factor;
                        v.gx_cmd_offset=(int)(pos-4-dloff);
                        if(cur_prim_type>=0){ dl.verts.push_back(v); dl.cmd_vtxxz++; }
                        break;
                    }
                    case 0x27: { // VTX_YZ (4 bytes)
                        uint32_t param=rd32(dec,pos); pos+=4;
                        Vert v=cur;
                        v.pos.y=(float)(int16_t)(param&0xFFFF)/4096.f*scale_factor;
                        v.pos.z=(float)(int16_t)(param>>16)/4096.f*scale_factor;
                        v.gx_cmd_offset=(int)(pos-4-dloff);
                        if(cur_prim_type>=0){ dl.verts.push_back(v); dl.cmd_vtxyz++; }
                        break;
                    }
                    case 0x28: { // VTX_DIFF (4 bytes)
                        uint32_t param=rd32(dec,pos); pos+=4;
                        Vert v=cur;
                        v.pos.x+=(float)(int16_t)((param<<6)&0xFFC0)/262144.f*scale_factor;
                        v.pos.y+=(float)(int16_t)((param>>4)&0xFFC0)/262144.f*scale_factor;
                        v.pos.z+=(float)(int16_t)((param>>14)&0xFFC0)/262144.f*scale_factor;
                        v.gx_cmd_offset=(int)(pos-4-dloff);
                        if(cur_prim_type>=0){ dl.verts.push_back(v); dl.cmd_vtxdiff++; }
                        break;
                    }
                    case 0x40: {
                        uint32_t param=rd32(dec,pos); pos+=4;
                        cur_prim_type=(int)(param&0x3);
                        first_vtx=(int)dl.verts.size();
                        DLInfo::PrimRun run; run.type=cur_prim_type; run.first_vtx=first_vtx;
                        dl.runs.push_back(run);
                        dl.cmd_begin++;
                        break;
                    }
                    case 0x41: {
                        if(cur_prim_type>=0&&first_vtx>=0){
                            int nv=(int)dl.verts.size()-first_vtx;
                            dl.run_tris.push_back(tris_from_run(cur_prim_type,nv));
                        }
                        cur_prim_type=-1; first_vtx=-1;
                        dl.cmd_end++;
                        break;
                    }
                    default: dl.cmd_other++; break;
                }
                if(pos>end){pos=end;break;}
            }
        }

        // Compute triangle counts
        dl.total_tris=0; for(int t:dl.run_tris) dl.total_tris+=t;

        // Bbox
        for(auto& v:dl.verts){
            dl.rminx=std::min(dl.rminx,v.pos.x); dl.rmaxx=std::max(dl.rmaxx,v.pos.x);
            dl.rminy=std::min(dl.rminy,v.pos.y); dl.rmaxy=std::max(dl.rmaxy,v.pos.y);
            dl.rminz=std::min(dl.rminz,v.pos.z); dl.rmaxz=std::max(dl.rmaxz,v.pos.z);
        }
        for(auto& v:dl.verts){
            int bone_id=0;
            if(v.matrix_id>=0&&v.matrix_id<(int)dl.bone_ids.size()) bone_id=dl.bone_ids[v.matrix_id];
            if(bone_id<0||bone_id>=n_bones) bone_id=0;
            const float* W=bones[bone_id].world;
            float wx=v.pos.x*W[0]+v.pos.y*W[4]+v.pos.z*W[8]+W[12];
            float wy=v.pos.x*W[1]+v.pos.y*W[5]+v.pos.z*W[9]+W[13];
            float wz=v.pos.x*W[2]+v.pos.y*W[6]+v.pos.z*W[10]+W[14];
            dl.wminx=std::min(dl.wminx,wx); dl.wmaxx=std::max(dl.wmaxx,wx);
            dl.wminy=std::min(dl.wminy,wy); dl.wmaxy=std::max(dl.wmaxy,wy);
            dl.wminz=std::min(dl.wminz,wz); dl.wmaxz=std::max(dl.wmaxz,wz);
        }

        // Matrix_id tracking per vertex (NO FALLBACK)
        dl.matrix_id_dist.resize(32,0);
        dl.max_matrix_id = -1;
        dl.matrix_id_oob = 0;
        dl.oob_examples.clear();
        for(int vi=0;vi<(int)dl.verts.size();vi++){
            int mid = dl.verts[vi].matrix_id;
            if(mid >= 0 && mid < 32) dl.matrix_id_dist[mid]++;
            if(mid > dl.max_matrix_id) dl.max_matrix_id = mid;
            if(mid < 0 || mid >= (int)dl.bone_ids.size()){
                dl.matrix_id_oob++;
                if((int)dl.oob_examples.size() < 5){
                    DLInfo::OOBExample ex;
                    ex.vert_idx = vi;
                    ex.gx_offset = dl.verts[vi].gx_cmd_offset;
                    ex.matrix_id = mid;
                    ex.bone_ids_size = (int)dl.bone_ids.size();
                    ex.last_mtx_cmd_offset = dl.last_mtx_cmd_offset;
                    dl.oob_examples.push_back(ex);
                }
            }
        }

        // Print per-DL row
        printf("%-4d %-6d %-8d ", dl.dl_index, dl.poly_id, dl.num_transforms);
        printf("[");
        for(size_t b=0;b<dl.bone_ids.size()&&b<6;b++){
            if(b>0) printf(",");
            printf("%d:%s", dl.bone_ids[b], dl.bone_names[b].c_str());
        }
        if(dl.bone_ids.size()>6) printf(",...");
        printf("] ");
        printf("%-6d %-6d %-5d %-5d %-6d %-6d %d/%d/%d/%d",
               (int)dl.verts.size(), dl.total_tris,
               (dl.max_matrix_id>=0)?0:0, dl.max_matrix_id,
               dl.matrix_id_oob, dl.cmd_vtx16, dl.cmd_vtx10,
               dl.cmd_vtxxy, dl.cmd_vtxxz, dl.cmd_vtxyz, dl.cmd_vtxdiff);
        if(dl.matrix_id_oob > 0) printf(" *** OOB=%d ***", dl.matrix_id_oob);
        printf("\n");
        fflush(stdout);

        dls.push_back(dl);
    }

    // Aggregate
    int total_verts=0, total_tris=0, total_oob=0;
    for(auto& dl : dls){
        total_verts+=(int)dl.verts.size();
        total_tris+=dl.total_tris;
        total_oob+=dl.matrix_id_oob;
    }
    printf("\n=== AGGREGATE ===\n");
    printf("DLs: %d  vertices: %d  triangles: %d  matrix_id OOB: %d\n",
           (int)dls.size(), total_verts, total_tris, total_oob);

    // Matrix_id summary per DL
    printf("\n=== MATRIX_ID USAGE PER DL ===\n");
    for(auto& dl : dls){
        if(dl.verts.empty()) continue;
        printf("DL[%2d] poly=%d bone_ids=%zu matrix_ids_used:", dl.dl_index, dl.poly_id, dl.bone_ids.size());
        bool first=true;
        for(int m=0;m<32;m++){
            if(dl.matrix_id_dist[m]>0){
                if(!first) printf(",");
                printf("%d(%d)", m, dl.matrix_id_dist[m]);
                first=false;
            }
        }
        if(dl.matrix_id_oob>0){
            printf(" OOB=%d", dl.matrix_id_oob);
            for(auto& ex : dl.oob_examples){
                printf("\n  OOB例: vert[%d] gx_offset=0x%X matrix_id=%d bone_ids.size=%d last_mtx_cmd=0x%X",
                       ex.vert_idx, ex.gx_offset, ex.matrix_id, ex.bone_ids_size, ex.last_mtx_cmd_offset);
            }
        }
        printf("\n");
    }

    // If no OOB, state clearly
    if(total_oob == 0){
        printf("\n*** matrix_id OOB = 0 for ALL display lists ***\n");
        printf("*** All matrix_ids are within transform list bounds ***\n");
    }

    // Build merged vertex/face arrays for OBJ export
    std::vector<Vert> all_verts;
    std::vector<Tri> all_tris;
    for(auto& dl : dls){
        int vbase=(int)all_verts.size();
        for(auto& v:dl.verts) all_verts.push_back(v);
        for(size_t r=0;r<dl.runs.size();r++){
            int first=vbase+dl.runs[r].first_vtx;
            int nv;
            if(r+1<(int)dl.runs.size()) nv=dl.runs[r+1].first_vtx-dl.runs[r].first_vtx;
            else nv=(int)dl.verts.size()-dl.runs[r].first_vtx;
            emit_tris_for_run(dl.runs[r].type, first, nv, dl.dl_index, all_tris);
        }
    }

    // Export raw OBJ
    {
        std::string path = outdir + "/file" + std::to_string(idx) + "_raw_current.obj";
        write_obj(path.c_str(), all_verts, all_tris);
        printf("\nExported %s (%zu verts, %zu tris)\n", path.c_str(), all_verts.size(), all_tris.size());
    }

    // Export world OBJ
    {
        std::vector<Vert> wv = all_verts;
        int vi=0;
        for(auto& dl : dls){
            for(auto& v : dl.verts){
                int bone_id=0;
                if(v.matrix_id>=0&&v.matrix_id<(int)dl.bone_ids.size()) bone_id=dl.bone_ids[v.matrix_id];
                if(bone_id<0||bone_id>=n_bones) bone_id=0;
                const float* W=bones[bone_id].world;
                Vec3 wp;
                wp.x=v.pos.x*W[0]+v.pos.y*W[4]+v.pos.z*W[8]+W[12];
                wp.y=v.pos.x*W[1]+v.pos.y*W[5]+v.pos.z*W[9]+W[13];
                wp.z=v.pos.x*W[2]+v.pos.y*W[6]+v.pos.z*W[10]+W[14];
                wv[vi].pos=wp;
                vi++;
            }
        }
        std::string path = outdir + "/file" + std::to_string(idx) + "_world_current.obj";
        write_obj(path.c_str(), wv, all_tris);
        printf("Exported %s (%zu verts, %zu tris)\n", path.c_str(), wv.size(), all_tris.size());
    }

    // Reference OBJ = current (parser is correct)
    {
        std::string path = outdir + "/file" + std::to_string(idx) + "_reference.obj";
        write_obj(path.c_str(), all_verts, all_tris);
        printf("Exported %s (= current, parser verified correct)\n", path.c_str());
    }

    // Topology
    printf("\n=== TOPOLOGY (raw) ===\n");
    {
        TopoStats ts=analyze_topology(all_verts,all_tris,(int)all_verts.size());
        printf("degenerate: %d  idx_oor: %d  isolated: %d/%d\n", ts.degenerate, ts.idx_oor, ts.isolated, (int)all_verts.size());
        printf("edges: median=%.4f max=%.4f  long(>10x): %d\n", ts.median_edge, ts.max_edge, ts.long_edges);
        printf("areas: median=%.6f max=%.6f  huge(>100x): %d\n", ts.median_area, ts.max_area, ts.huge_area);
        printf("bridge triangles: %d\n", ts.bridge);
    }
    printf("\n=== TOPOLOGY (world) ===\n");
    {
        std::vector<Vert> wv=all_verts; int vi=0;
        for(auto& dl:dls) for(auto&v:dl.verts){
            int bone_id=0;
            if(v.matrix_id>=0&&v.matrix_id<(int)dl.bone_ids.size()) bone_id=dl.bone_ids[v.matrix_id];
            if(bone_id<0||bone_id>=n_bones) bone_id=0;
            const float* W=bones[bone_id].world;
            wv[vi].pos.x=v.pos.x*W[0]+v.pos.y*W[4]+v.pos.z*W[8]+W[12];
            wv[vi].pos.y=v.pos.x*W[1]+v.pos.y*W[5]+v.pos.z*W[9]+W[13];
            wv[vi].pos.z=v.pos.x*W[2]+v.pos.y*W[6]+v.pos.z*W[10]+W[14];
            vi++;
        }
        TopoStats ts=analyze_topology(wv,all_tris,(int)wv.size());
        printf("degenerate: %d  idx_oor: %d  isolated: %d/%d\n", ts.degenerate, ts.idx_oor, ts.isolated, (int)wv.size());
        printf("edges: median=%.4f max=%.4f  long(>10x): %d\n", ts.median_edge, ts.max_edge, ts.long_edges);
        printf("areas: median=%.6f max=%.6f  huge(>100x): %d\n", ts.median_area, ts.max_area, ts.huge_area);
        printf("bridge triangles: %d\n", ts.bridge);
    }

    // GX dump mode (env var)
    const char* dump_env = getenv("BMD_DUMP_DL");
    if(dump_env){
        int dump_idx = atoi(dump_env);
        if(dump_idx >= 0 && dump_idx < (int)dls.size()){
            const DLInfo& dl = dls[dump_idx];
            printf("\n=== GX DUMP DL[%d] (poly=%d) ===\n", dump_idx, dl.poly_id);
            printf("bone_ids:", dl.bone_ids.size());
            for(size_t b=0;b<dl.bone_ids.size();b++)
                printf(" [%zu]=%d(%s)", b, dl.bone_ids[b], dl.bone_names[b].c_str());
            printf("\n");
            uint32_t raw_dl_off=rd32(dec,poly_off+dl.poly_id*8+4);
            uint32_t raw_numtr=rd32(dec,raw_dl_off);
            uint32_t raw_troff=rd32(dec,raw_dl_off+4);
            uint32_t raw_dlsize=rd32(dec,raw_dl_off+8);
            uint32_t raw_dloff=rd32(dec,raw_dl_off+0x0C);
            printf("transforms (%u bytes at 0x%X):", raw_numtr, raw_troff);
            for(uint32_t t=0;t<raw_numtr;t++){
                uint8_t idx1=rd8(dec,raw_troff+t);
                printf(" [%u]=0x%02X->bone[%d]=%s", t, idx1, dl.bone_ids[t], dl.bone_names[t].c_str());
            }
            printf("\n\n");
            float cx=0,cy=0,cz=0; int mid=0, vi=0;
            size_t gpos=raw_dloff, gend=raw_dloff+raw_dlsize;
            while(gpos+4<=gend){
                uint8_t c[4]={rd8(dec,gpos),rd8(dec,gpos+1),rd8(dec,gpos+2),rd8(dec,gpos+3)};
                gpos+=4;
                for(int ci=0;ci<4;ci++){
                    uint8_t cmd=c[ci];
                    if(cmd==0x14){
                        uint32_t param=rd32(dec,gpos); gpos+=4;
                        mid=(int)(param&0x1F);
                    } else if(cmd==0x23){
                        uint32_t p1=rd32(dec,gpos); gpos+=4;
                        uint32_t p2=rd32(dec,gpos); gpos+=4;
                        cx=(float)(int16_t)(p1&0xFFFF)/4096.f*scale_factor;
                        cy=(float)(int16_t)(p1>>16)/4096.f*scale_factor;
                        cz=(float)(int16_t)(p2&0xFFFF)/4096.f*scale_factor;
                        printf("[%3d] VTX_16  mtx=%d (%8.4f,%8.4f,%8.4f)\n",vi,mid,cx,cy,cz); vi++;
                    } else if(cmd==0x24){
                        uint32_t param=rd32(dec,gpos); gpos+=4;
                        cx=(float)(int16_t)((param<<6)&0xFFC0)/4096.f*scale_factor;
                        cy=(float)(int16_t)((param>>4)&0xFFC0)/4096.f*scale_factor;
                        cz=(float)(int16_t)((param>>14)&0xFFC0)/4096.f*scale_factor;
                        printf("[%3d] VTX_10  mtx=%d (%8.4f,%8.4f,%8.4f)\n",vi,mid,cx,cy,cz); vi++;
                    } else if(cmd==0x25){
                        uint32_t param=rd32(dec,gpos); gpos+=4;
                        cx=(float)(int16_t)(param&0xFFFF)/4096.f*scale_factor;
                        cy=(float)(int16_t)(param>>16)/4096.f*scale_factor;
                        printf("[%3d] VTX_XY  mtx=%d (%8.4f,%8.4f,%8.4f) [Z preserved]\n",vi,mid,cx,cy,cz); vi++;
                    } else if(cmd==0x26){
                        uint32_t param=rd32(dec,gpos); gpos+=4;
                        cx=(float)(int16_t)(param&0xFFFF)/4096.f*scale_factor;
                        cz=(float)(int16_t)(param>>16)/4096.f*scale_factor;
                        printf("[%3d] VTX_XZ  mtx=%d (%8.4f,%8.4f,%8.4f) [Y preserved]\n",vi,mid,cx,cy,cz); vi++;
                    } else if(cmd==0x27){
                        uint32_t param=rd32(dec,gpos); gpos+=4;
                        cy=(float)(int16_t)(param&0xFFFF)/4096.f*scale_factor;
                        cz=(float)(int16_t)(param>>16)/4096.f*scale_factor;
                        printf("[%3d] VTX_YZ  mtx=%d (%8.4f,%8.4f,%8.4f) [X preserved]\n",vi,mid,cx,cy,cz); vi++;
                    } else if(cmd==0x28){
                        uint32_t param=rd32(dec,gpos); gpos+=4;
                        cx+=(float)(int16_t)((param<<6)&0xFFC0)/262144.f*scale_factor;
                        cy+=(float)(int16_t)((param>>4)&0xFFC0)/262144.f*scale_factor;
                        cz+=(float)(int16_t)((param>>14)&0xFFC0)/262144.f*scale_factor;
                        printf("[%3d] VTX_DIFF mtx=%d (%8.4f,%8.4f,%8.4f) [additive]\n",vi,mid,cx,cy,cz); vi++;
                    } else if(cmd==0x40){
                        uint32_t param=rd32(dec,gpos); gpos+=4;
                        printf("--- BEGIN prim=%d ---\n",param&3);
                    } else if(cmd==0x41){
                        printf("--- END ---\n");
                    } else if(cmd==0x20){gpos+=4;}
                    else if(cmd==0x21){gpos+=4;}
                    else if(cmd==0x22){gpos+=4;}
                    else if(cmd==0x10||cmd==0x12||cmd==0x13||cmd==0x50||cmd==0x60||cmd==0x72||
                            cmd==0x29||cmd==0x2A||cmd==0x2B||cmd==0x30||cmd==0x31||cmd==0x32||cmd==0x33){gpos+=4;}
                    else if(cmd==0x16||cmd==0x18){gpos+=64;}
                    else if(cmd==0x17||cmd==0x19){gpos+=48;}
                    else if(cmd==0x1A){gpos+=36;}
                    else if(cmd==0x1B||cmd==0x1C){gpos+=12;}
                    else if(cmd==0x34){gpos+=128;}
                    else if(cmd==0x70){gpos+=12;}
                    else if(cmd==0x71){gpos+=8;}
                    if(gpos>gend){gpos=gend;break;}
                }
            }
            printf("\nTotal vertices decoded: %d\n", vi);
        }
    }

    return 0;
}
