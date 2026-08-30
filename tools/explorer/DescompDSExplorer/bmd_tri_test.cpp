// bmd_tri_test.cpp — Independent triangulation reference + comparison
// Parses BMD, emits vertices per run, triangulates with TWO independent implementations:
//   A) "current" — our bmd_validate.cpp emit_tris_for_run
//   B) "reference" — SM64DSe/OpenGL standard rules
// Compares them, exports both OBJs, reports differences per DL.
#include "romfile.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>
#include <algorithm>

struct Vec3 { float x=0,y=0,z=0; };
struct Vert { Vec3 pos; int matrix_id=0; int dl=0; };
struct Tri { int v[3]; int dl; };

// ---- Read helpers ----
uint32_t rd32(const std::vector<uint8_t>& d,size_t o){
    if(o+4>d.size()) return 0;
    return (uint32_t)d[o]|((uint32_t)d[o+1]<<8)|((uint32_t)d[o+2]<<16)|((uint32_t)d[o+3]<<24);
}
int32_t rds32(const std::vector<uint8_t>& d,size_t o){ return(int32_t)rd32(d,o); }
int16_t rds16(const std::vector<uint8_t>& d,size_t o){ return(int16_t)((uint16_t)(d[o]|(d[o+1]<<8))); }
uint8_t rd8(const std::vector<uint8_t>& d,size_t o){ return o<d.size()?d[o]:0; }

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

struct Bone {
    float scale[3]={1,1,1}; short rot[3]={0,0,0}; float trans[3]={0,0,0};
    float world[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    int parent_offset=0;
};

// ---- OBJ writer ----
void write_obj(const char* path, const std::vector<Vert>& verts, const std::vector<Tri>& tris){
    FILE* f=fopen(path,"w"); if(!f) return;
    fprintf(f,"# bmd_tri_test reference output\n# verts=%zu tris=%zu\n\n",verts.size(),tris.size());
    for(auto& v:verts) fprintf(f,"v %.6f %.6f %.6f\n",v.pos.x,v.pos.y,v.pos.z);
    fprintf(f,"\n");
    int cur=-1;
    for(auto& t:tris){
        if(t.dl!=cur){ cur=t.dl; fprintf(f,"\ng dl_%d\n",cur); }
        fprintf(f,"f %d %d %d\n",t.v[0]+1,t.v[1]+1,t.v[2]+1);
    }
    fclose(f);
}

// ============================================================
// TRIANGULATION A: CURRENT (our bmd_validate emit_tris_for_run)
// ============================================================
void emit_current(int prim_type, int first, int count, int dl, std::vector<Tri>& out){
    if(prim_type==0){
        for(int i=0;i+2<count;i+=3){
            Tri t; t.v[0]=first+i; t.v[1]=first+i+1; t.v[2]=first+i+2; t.dl=dl;
            out.push_back(t);
        }
    } else if(prim_type==1){
        for(int i=0;i+3<count;i+=4){
            Tri t; t.v[0]=first+i; t.v[1]=first+i+1; t.v[2]=first+i+2; t.dl=dl; out.push_back(t);
            Tri t2; t2.v[0]=first+i; t2.v[1]=first+i+2; t2.v[2]=first+i+3; t2.dl=dl; out.push_back(t2);
        }
    } else if(prim_type==2){
        for(int i=0;i+2<count;i++){
            Tri t;
            if(i%2==0){ t.v[0]=first+i; t.v[1]=first+i+1; t.v[2]=first+i+2; }
            else       { t.v[0]=first+i+1; t.v[1]=first+i; t.v[2]=first+i+2; }
            t.dl=dl; out.push_back(t);
        }
    } else if(prim_type==3){
        for(int i=2;i+1<count;i+=2){
            Tri ta; ta.v[0]=first+i-2; ta.v[1]=first+i; ta.v[2]=first+i+1; ta.dl=dl; out.push_back(ta);
            Tri tb; tb.v[0]=first+i-2; tb.v[1]=first+i+1; tb.v[2]=first+i-1; tb.dl=dl; out.push_back(tb);
        }
    }
}

// ============================================================
// TRIANGULATION B: REFERENCE (SM64DSe/OpenGL standard)
// OpenGL spec: Triangles=groups of 3, Quads=groups of 4 diagonal,
// TriStrip even=(i,i+1,i+2) odd=(i+1,i,i+2),
// QuadStrip=interleaved pairs: quad=(i,i+2,i+3,i+1) → (i,i+2,i+3)+(i,i+3,i+1)
// ============================================================
void emit_reference(int prim_type, int first, int count, int dl, std::vector<Tri>& out){
    if(prim_type==0){
        // Triangles: groups of 3, same as current
        for(int i=0;i+2<count;i+=3){
            Tri t; t.v[0]=first+i; t.v[1]=first+i+1; t.v[2]=first+i+2; t.dl=dl;
            out.push_back(t);
        }
    } else if(prim_type==1){
        // Quads: groups of 4, diagonal (i,i+2)
        // Same as current: (i,i+1,i+2) + (i,i+2,i+3)
        for(int i=0;i+3<count;i+=4){
            Tri t; t.v[0]=first+i; t.v[1]=first+i+1; t.v[2]=first+i+2; t.dl=dl; out.push_back(t);
            Tri t2; t2.v[0]=first+i; t2.v[1]=first+i+2; t2.v[2]=first+i+3; t2.dl=dl; out.push_back(t2);
        }
    } else if(prim_type==2){
        // Triangle strip: same even/odd rule as current
        // Both OpenGL and our code agree: even=(i,i+1,i+2), odd=(i+1,i,i+2)
        for(int i=0;i+2<count;i++){
            Tri t;
            if(i%2==0){ t.v[0]=first+i; t.v[1]=first+i+1; t.v[2]=first+i+2; }
            else       { t.v[0]=first+i+1; t.v[1]=first+i; t.v[2]=first+i+2; }
            t.dl=dl; out.push_back(t);
        }
    } else if(prim_type==3){
        // Quad strip: INTERLEAVED pairs
        // Vertices: pair0=(v0,v1), pair1=(v2,v3), pair2=(v4,v5)...
        // Quad k is formed by pair(k-1) and pair(k):
        //   vertices = (v_{2k-2}, v_{2k}, v_{2k+1}, v_{2k-1})
        //   tri A = (v_{2k-2}, v_{2k}, v_{2k+1})
        //   tri B = (v_{2k-2}, v_{2k+1}, v_{2k-1})
        for(int i=2;i+1<count;i+=2){
            // quad = (first+i-2, first+i, first+i+1, first+i-1)
            Tri ta; ta.v[0]=first+i-2; ta.v[1]=first+i; ta.v[2]=first+i+1; ta.dl=dl; out.push_back(ta);
            Tri tb; tb.v[0]=first+i-2; tb.v[1]=first+i+1; tb.v[2]=first+i-1; tb.dl=dl; out.push_back(tb);
        }
    }
}

// ---- Edge length ----
float edge_len(const Vec3& a, const Vec3& b){
    float dx=a.x-b.x,dy=a.y-b.y,dz=a.z-b.z; return sqrtf(dx*dx+dy*dy+dz*dz);
}
float tri_area(const Vec3& a, const Vec3& b, const Vec3& c){
    float ux=b.x-a.x,uy=b.y-a.y,uz=b.z-a.z;
    float vx=c.x-a.x,vy=c.y-a.y,vz=c.z-a.z;
    float nx=uy*vz-uz*vy,ny=uz*vx-ux*vz,nz=ux*vy-uy*vx;
    return 0.5f*sqrtf(nx*nx+ny*ny+nz*nz);
}
// Signed volume (for winding check)
float signed_vol(const Vec3& a, const Vec3& b, const Vec3& c){
    return a.x*(b.y*c.z - b.z*c.y) - a.y*(b.x*c.z - b.z*c.x) + a.z*(b.x*c.y - b.y*c.x);
}

int main(int argc, char** argv){
    if(argc<3){ printf("usage: bmd_tri_test <rom> <file_index>\n"); return 2; }
    int idx=atoi(argv[2]);
    std::vector<uint8_t> raw;
    if(!romfile::read_file(argv[1],idx,raw)){ printf("ROM read fail\n"); return 1; }
    auto dec=romfile::decompress(raw);
    printf("file[%d] compressed=%zu decompressed=%zu\n",idx,raw.size(),dec.size());
    if(dec.size()<60){ printf("BMD too small\n"); return 1; }

    int scale_shift=rds32(dec,0x00);
    float sf=(float)(1<<scale_shift);
    int n_bones=rds32(dec,0x04);
    uint32_t bones_off=rd32(dec,0x08);
    int n_poly=rds32(dec,0x0C);
    uint32_t poly_off=rd32(dec,0x10);
    uint32_t bonemap_off=rd32(dec,0x2C);
    printf("scale_shift=%d n_bones=%d n_poly=%d\n",scale_shift,n_bones,n_poly);

    // Bones
    std::vector<Bone> bones(n_bones);
    for(int i=0;i<n_bones;i++){
        size_t bo=bones_off+(size_t)i*64;
        bones[i].parent_offset=rds16(dec,bo+8);
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
        bonemap.push_back((uint16_t)(dec[off]|(dec[off+1]<<8)));
    }

    // Parse DLs, emit vertices per run
    struct RunInfo { int type; int first_vtx; int nv; int dl_idx; int poly_id; };
    std::vector<Vert> all_verts;
    std::vector<RunInfo> runs;

    for(int pi=0;pi<n_poly;pi++){
        size_t po=poly_off+(size_t)pi*8;
        if(po+8>dec.size()) break;
        uint32_t dl_hdr=rd32(dec,po+4);
        if(dl_hdr+16>dec.size()) continue;
        uint32_t num_tr=rd32(dec,dl_hdr);
        uint32_t tr_off=rd32(dec,dl_hdr+4);
        uint32_t dl_sz=rd32(dec,dl_hdr+8);
        uint32_t dl_off=rd32(dec,dl_hdr+0x0C);

        std::vector<int> bone_ids;
        for(uint32_t t=0;t<num_tr;t++){
            uint8_t idx1=rd8(dec,tr_off+t);
            uint16_t bid=(idx1<bonemap.size())?bonemap[idx1]:0;
            bone_ids.push_back((bid<(uint32_t)n_bones)?(int)bid:0);
        }

        // Walk GX
        Vec3 cur_pos={0,0,0}; int cur_mtx=0;
        int cur_prim=-1; int first_v=-1;
        size_t pos=dl_off, end=dl_off+dl_sz;
        int dl_verts_start=(int)all_verts.size();

        while(pos+4<=end){
            uint8_t c[4]={rd8(dec,pos),rd8(dec,pos+1),rd8(dec,pos+2),rd8(dec,pos+3)};
            pos+=4;
            for(int ci=0;ci<4;ci++){
                uint8_t cmd=c[ci];
                switch(cmd){
                    case 0x00: break;
                    case 0x10: case 0x12: case 0x13: pos+=4; break;
                    case 0x14: { uint32_t p=rd32(dec,pos); pos+=4; cur_mtx=(int)(p&0x1F); break; }
                    case 0x16: pos+=64; break;
                    case 0x17: case 0x19: pos+=48; break;
                    case 0x18: pos+=64; break;
                    case 0x1A: pos+=36; break;
                    case 0x1B: case 0x1C: case 0x20: case 0x21: case 0x22:
                    case 0x29: case 0x2A: case 0x2B:
                    case 0x30: case 0x31: case 0x32: case 0x33: case 0x72: pos+=4; break;
                    case 0x34: pos+=128; break;
                    case 0x50: case 0x60: pos+=4; break;
                    case 0x70: pos+=12; break;
                    case 0x71: pos+=8; break;
                    case 0x23: {
                        uint32_t p1=rd32(dec,pos); pos+=4;
                        uint32_t p2=rd32(dec,pos); pos+=4;
                        Vert v; v.dl=pi; v.matrix_id=cur_mtx;
                        v.pos.x=(float)(int16_t)(p1&0xFFFF)/4096.f*sf;
                        v.pos.y=(float)(int16_t)(p1>>16)/4096.f*sf;
                        v.pos.z=(float)(int16_t)(p2&0xFFFF)/4096.f*sf;
                        if(cur_prim>=0) all_verts.push_back(v);
                        break;
                    }
                    case 0x24: {
                        uint32_t p=rd32(dec,pos); pos+=4;
                        Vert v; v.dl=pi; v.matrix_id=cur_mtx;
                        v.pos.x=(float)(int16_t)((p<<6)&0xFFC0)/4096.f*sf;
                        v.pos.y=(float)(int16_t)((p>>4)&0xFFC0)/4096.f*sf;
                        v.pos.z=(float)(int16_t)((p>>14)&0xFFC0)/4096.f*sf;
                        if(cur_prim>=0) all_verts.push_back(v);
                        break;
                    }
                    case 0x25: {
                        uint32_t p=rd32(dec,pos); pos+=4;
                        Vert v; v.dl=pi; v.matrix_id=cur_mtx;
                        v.pos.x=(float)(int16_t)(p&0xFFFF)/4096.f*sf;
                        v.pos.y=(float)(int16_t)(p>>16)/4096.f*sf;
                        v.pos.z=cur_pos.z;
                        if(cur_prim>=0) all_verts.push_back(v);
                        cur_pos={v.pos.x,v.pos.y,v.pos.z};
                        break;
                    }
                    case 0x26: {
                        uint32_t p=rd32(dec,pos); pos+=4;
                        Vert v; v.dl=pi; v.matrix_id=cur_mtx;
                        v.pos.x=(float)(int16_t)(p&0xFFFF)/4096.f*sf;
                        v.pos.z=(float)(int16_t)(p>>16)/4096.f*sf;
                        v.pos.y=cur_pos.y;
                        if(cur_prim>=0) all_verts.push_back(v);
                        cur_pos={v.pos.x,v.pos.y,v.pos.z};
                        break;
                    }
                    case 0x27: {
                        uint32_t p=rd32(dec,pos); pos+=4;
                        Vert v; v.dl=pi; v.matrix_id=cur_mtx;
                        v.pos.y=(float)(int16_t)(p&0xFFFF)/4096.f*sf;
                        v.pos.z=(float)(int16_t)(p>>16)/4096.f*sf;
                        v.pos.x=cur_pos.x;
                        if(cur_prim>=0) all_verts.push_back(v);
                        cur_pos={v.pos.x,v.pos.y,v.pos.z};
                        break;
                    }
                    case 0x28: {
                        uint32_t p=rd32(dec,pos); pos+=4;
                        cur_pos.x+=(float)(int16_t)((p<<6)&0xFFC0)/262144.f*sf;
                        cur_pos.y+=(float)(int16_t)((p>>4)&0xFFC0)/262144.f*sf;
                        cur_pos.z+=(float)(int16_t)((p>>14)&0xFFC0)/262144.f*sf;
                        Vert v; v.dl=pi; v.matrix_id=cur_mtx; v.pos=cur_pos;
                        if(cur_prim>=0) all_verts.push_back(v);
                        break;
                    }
                    case 0x40: {
                        uint32_t p=rd32(dec,pos); pos+=4;
                        cur_prim=(int)(p&3); first_v=(int)all_verts.size();
                        break;
                    }
                    case 0x41: {
                        if(cur_prim>=0&&first_v>=0){
                            int nv=(int)all_verts.size()-first_v;
                            RunInfo r; r.type=cur_prim; r.first_vtx=first_v; r.nv=nv;
                            r.dl_idx=pi; r.poly_id=pi;
                            runs.push_back(r);
                        }
                        cur_prim=-1; first_v=-1;
                        break;
                    }
                    default: break;
                }
                if(pos>end){pos=end;break;}
            }
            // Track current position for VTX_XY/XZ/YZ preservation
            if(!all_verts.empty()) cur_pos=all_verts.back().pos;
        }
    }

    printf("Total vertices: %zu  Runs: %zu\n",all_verts.size(),runs.size());

    // Triangulate with BOTH methods
    std::vector<Tri> tris_current, tris_reference;
    for(auto& r : runs){
        emit_current(r.type, r.first_vtx, r.nv, r.dl_idx, tris_current);
        emit_reference(r.type, r.first_vtx, r.nv, r.dl_idx, tris_reference);
    }
    printf("Current triangles: %zu  Reference triangles: %zu\n",tris_current.size(),tris_reference.size());

    // Compare per DL
    printf("\n=== TRIANGULATION COMPARISON PER DL ===\n");
    for(int pi=0;pi<n_poly;pi++){
        // Count tris for this DL
        int cnt_c=0,cnt_r=0;
        for(auto& t:tris_current) if(t.dl==pi) cnt_c++;
        for(auto& t:tris_reference) if(t.dl==pi) cnt_r++;

        // Find runs for this DL
        int run_count=0;
        for(auto& r:runs) if(r.dl_idx==pi) run_count++;

        if(cnt_c==cnt_r){
            // Same count — compare vertex indices
            bool match=true;
            int idx_c=0,idx_r=0;
            for(int ti=0;ti<(int)tris_current.size();ti++){
                if(tris_current[ti].dl!=pi) continue;
                while(idx_r<(int)tris_reference.size()&&tris_reference[idx_r].dl!=pi) idx_r++;
                if(idx_r>=(int)tris_reference.size()){ match=false; break; }
                auto& tc=tris_current[ti];
                auto& tr=tris_reference[idx_r];
                if(tc.v[0]!=tr.v[0]||tc.v[1]!=tr.v[1]||tc.v[2]!=tr.v[2]) match=false;
                idx_r++;
            }
            printf("DL[%2d] poly=%d runs=%d current=%d ref=%d %s\n",
                   pi,pi,run_count,cnt_c,cnt_r,match?"MATCH":"DIFFERENT");
        } else {
            printf("DL[%2d] poly=%d runs=%d current=%d ref=%d COUNT_MISMATCH\n",
                   pi,pi,run_count,cnt_c,cnt_r);
        }
    }

    // Export both OBJs
    write_obj("C:/Users/Jorge/AppData/Local/Temp/opencode/file175_tri_current.obj", all_verts, tris_current);
    write_obj("C:/Users/Jorge/AppData/Local/Temp/opencode/file175_tri_reference.obj", all_verts, tris_reference);
    printf("\nExported file175_tri_current.obj and file175_tri_reference.obj\n");

    // DL[16] detailed dump (if present)
    bool has_dl16=false;
    for(auto& r:runs) if(r.dl_idx==16) has_dl16=true;
    if(has_dl16){
        printf("\n=== DL[16] DETAILED COMPARISON ===\n");
        // Find the runs for DL[16]
        int run_idx=0;
        for(auto& r:runs){
            if(r.dl_idx==16){
                printf("Run %d: type=%d first_vtx=%d nv=%d\n",run_idx,r.type,r.first_vtx,r.nv);
                // Emit both triangulations for this run
                std::vector<Tri> tc,tr;
                emit_current(r.type, r.first_vtx, r.nv, 16, tc);
                emit_reference(r.type, r.first_vtx, r.nv, 16, tr);
                printf("  Current: %zu tris  Reference: %zu tris\n",tc.size(),tr.size());
                // Print first few triangles from each
                int show=std::min(10,(int)tc.size());
                for(int i=0;i<show;i++){
                    printf("  C tri[%d]: v%d,v%d,v%d  pos=(%.3f,%.3f,%.3f)-(%.3f,%.3f,%.3f)-(%.3f,%.3f,%.3f)\n",
                           i,tc[i].v[0],tc[i].v[1],tc[i].v[2],
                           all_verts[tc[i].v[0]].pos.x,all_verts[tc[i].v[0]].pos.y,all_verts[tc[i].v[0]].pos.z,
                           all_verts[tc[i].v[1]].pos.x,all_verts[tc[i].v[1]].pos.y,all_verts[tc[i].v[1]].pos.z,
                           all_verts[tc[i].v[2]].pos.x,all_verts[tc[i].v[2]].pos.y,all_verts[tc[i].v[2]].pos.z);
                }
                if((int)tr.size()>0){
                    show=std::min(10,(int)tr.size());
                    for(int i=0;i<show;i++){
                        printf("  R tri[%d]: v%d,v%d,v%d  pos=(%.3f,%.3f,%.3f)-(%.3f,%.3f,%.3f)-(%.3f,%.3f,%.3f)\n",
                               i,tr[i].v[0],tr[i].v[1],tr[i].v[2],
                               all_verts[tr[i].v[0]].pos.x,all_verts[tr[i].v[0]].pos.y,all_verts[tr[i].v[0]].pos.z,
                               all_verts[tr[i].v[1]].pos.x,all_verts[tr[i].v[1]].pos.y,all_verts[tr[i].v[1]].pos.z,
                               all_verts[tr[i].v[2]].pos.x,all_verts[tr[i].v[2]].pos.y,all_verts[tr[i].v[2]].pos.z);
                    }
                }
                run_idx++;
            }
        }
    }

    // File[1933] extreme vertices
    if(idx==1933){
        printf("\n=== FILE[1933] EXTREME VERTEX ANALYSIS ===\n");
        printf("scale_factor=%.0f (1<<%d)\n",sf,scale_shift);
        // Find max abs coordinate
        float max_abs=0;
        for(auto& v:all_verts){
            float a=std::max({fabsf(v.pos.x),fabsf(v.pos.y),fabsf(v.pos.z)});
            if(a>max_abs) max_abs=a;
        }
        printf("Max abs coordinate: %.1f\n",max_abs);
        // Find vertices with abs>50
        printf("Vertices with abs>50:\n");
        for(int vi=0;vi<(int)all_verts.size();vi++){
            auto& v=all_verts[vi];
            float a=std::max({fabsf(v.pos.x),fabsf(v.pos.y),fabsf(v.pos.z)});
            if(a>50){
                printf("  vert[%d] dl=%d mtx=%d pos=(%.1f,%.1f,%.1f)\n",
                       vi,v.dl,v.matrix_id,v.pos.x,v.pos.y,v.pos.z);
            }
        }
    }

    return 0;
}
