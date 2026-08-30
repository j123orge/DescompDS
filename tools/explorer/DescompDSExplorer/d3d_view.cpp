// DescompDS Explorer — D3D11 model view renderer implementation
#include "d3d_view.h"

#include <d3dcompiler.h>
#include <dxgi.h>
#include <DirectXMath.h>
#include <vector>
#include <algorithm>
#include <cmath>
#include <fstream>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

using namespace DirectX;

namespace d3dview {

namespace {

ID3D11Device* g_dev = nullptr;
ID3D11DeviceContext* g_ctx = nullptr;
IDXGISwapChain* g_swap = nullptr;
ID3D11RenderTargetView* g_rtv = nullptr;
ID3D11DepthStencilView* g_dsv = nullptr;

ID3D11VertexShader* g_vs = nullptr;
ID3D11PixelShader* g_ps = nullptr;
ID3D11InputLayout* g_layout = nullptr;
ID3D11Buffer* g_vb = nullptr;
ID3D11Buffer* g_ib = nullptr;
ID3D11Buffer* g_cb = nullptr;

// model
ID3D11Buffer* g_model_vb = nullptr;
ID3D11Buffer* g_model_ib = nullptr;
UINT g_model_vcount = 0, g_model_icount = 0;

// debug geometry (axes + bbox): line list
ID3D11Buffer* g_dbg_vb = nullptr;
UINT g_dbg_vcount = 0;

int g_render_mode = 1;
const char* g_save_path = nullptr;

// camera
XMFLOAT3 g_center = {0, 0, 0};
float g_yaw = 0.6f, g_pitch = 0.3f, g_dist = 12.0f;
float g_fovy = 0.7f;
int g_win_w = 800, g_win_h = 600;

struct CB { XMMATRIX W; XMMATRIX V; XMMATRIX P; XMFLOAT4 color; };

struct PVertex { float x, y, z; float nx, ny, nz; };

const char* k_vs_src =
    "cbuffer CB : register(b0) { row_major float4x4 W; row_major float4x4 V; row_major float4x4 P; float4 Color; };\n"
    "struct VIn { float3 p : POS; float3 n : NORMAL; };\n"
    "struct VOut { float4 p : SV_Position; float3 n : NORMAL; };\n"
    "VOut main(VIn i) { VOut o; float4 wp = mul(float4(i.p,1), W); "
    "o.p = mul(mul(wp, V), P); o.n = i.n; return o; }\n";

const char* k_ps_src =
    "cbuffer CB : register(b0) { row_major float4x4 W; row_major float4x4 V; row_major float4x4 P; float4 Color; };\n"
    "struct VOut { float4 p : SV_Position; float3 n : NORMAL; };\n"
    "float4 main(VOut i) : SV_Target { "
    "float l = (Color.a > 0.5) ? 1.0 : max(dot(normalize(i.n), float3(0.3,0.5,0.8)), 0.15); "
    "return float4(Color.rgb * l, 1.0); }\n";

bool create_shaders() {
    ID3DBlob* vsb = nullptr; ID3DBlob* psb = nullptr; ID3DBlob* err = nullptr;
    HRESULT hr = D3DCompile(k_vs_src, std::strlen(k_vs_src), nullptr, nullptr, nullptr,
                            "main", "vs_4_0", 0, 0, &vsb, &err);
    if (FAILED(hr)) { if (err) err->Release(); return false; }
    hr = D3DCompile(k_ps_src, std::strlen(k_ps_src), nullptr, nullptr, nullptr,
                    "main", "ps_4_0", 0, 0, &psb, &err);
    if (FAILED(hr)) { if (err) err->Release(); return false; }

    g_dev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &g_vs);
    g_dev->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &g_ps);

    D3D11_INPUT_ELEMENT_DESC desc[] = {
        {"POS", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    g_dev->CreateInputLayout(desc, 2, vsb->GetBufferPointer(), vsb->GetBufferSize(), &g_layout);

    D3D11_BUFFER_DESC cbd{};
    cbd.ByteWidth = sizeof(CB); cbd.Usage = D3D11_USAGE_DYNAMIC;
    cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER; cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    g_dev->CreateBuffer(&cbd, nullptr, &g_cb);
    return true;
}

void make_debug_geometry() {
    // axes (3 lines) + bbox (12 lines) + test cube (12 lines)
    std::vector<PVertex> v;
    auto seg = [&](float x1,float y1,float z1,float x2,float y2,float z2){
        PVertex a{x1,y1,z1,0,0,0}; PVertex b{x2,y2,z2,0,0,0};
        v.push_back(a); v.push_back(b);
    };
    // test cube (unit, origin-centered) to prove D3D+camera work
    auto cube = [&](float s){
        float h = s*0.5f;
        float x0=-h,x1=h,y0=-h,y1=h,z0=-h,z1=h;
        seg(x0,y0,z0,x1,y0,z0); seg(x0,y0,z0,x0,y1,z0); seg(x0,y0,z0,x0,y0,z1);
        seg(x1,y0,z0,x1,y1,z0); seg(x1,y0,z0,x1,y0,z1);
        seg(x0,y1,z0,x1,y1,z0); seg(x0,y1,z0,x0,y1,z1);
        seg(x1,y1,z0,x1,y1,z1); seg(x1,y1,z0,x1,y0,z1);
        seg(x0,y0,z1,x1,y0,z1); seg(x0,y0,z1,x0,y1,z1);
        seg(x1,y0,z1,x1,y1,z1); seg(x1,y1,z1,x0,y1,z1); seg(x1,y1,z1,x1,y0,z1);
    };
    cube(0.5f);
    // axes, length = radius
    float L = g_dist * 0.5f;
    seg(0,0,0,L,0,0); seg(0,0,0,0,L,0); seg(0,0,0,0,0,L);
    // bbox
    if (g_center.x != 0 || g_center.y != 0 || g_center.z != 0) {
        // compute bbox from model radius around center
        float r = g_dist * 0.4f;
        float x0=g_center.x-r,x1=g_center.x+r,y0=g_center.y-r,y1=g_center.y+r,z0=g_center.z-r,z1=g_center.z+r;
        seg(x0,y0,z0,x1,y0,z0); seg(x0,y0,z0,x0,y1,z0); seg(x0,y0,z0,x0,y0,z1);
        seg(x1,y0,z0,x1,y1,z0); seg(x1,y0,z0,x1,y0,z1);
        seg(x0,y1,z0,x1,y1,z0); seg(x0,y1,z0,x0,y1,z1);
        seg(x1,y1,z0,x1,y1,z1); seg(x1,y1,z0,x1,y0,z1);
        seg(x0,y0,z1,x1,y0,z1); seg(x0,y0,z1,x0,y1,z1);
        seg(x1,y0,z1,x1,y1,z1); seg(x1,y1,z1,x0,y1,z1); seg(x1,y1,z1,x1,y0,z1);
    }
    g_dbg_vcount = (UINT)v.size();
    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = (UINT)(v.size()*sizeof(PVertex));
    bd.Usage = D3D11_USAGE_IMMUTABLE; bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA sd{v.data(),0,0};
    g_dev->CreateBuffer(&bd,&sd,&g_dbg_vb);
}

} // namespace

bool init(HWND hwnd) {
    RECT rc; GetClientRect(hwnd, &rc);
    g_win_w = rc.right - rc.left; g_win_h = rc.bottom - rc.top;
    if (g_win_w < 8) g_win_w = 800; if (g_win_h < 8) g_win_h = 600;

    DXGI_SWAP_CHAIN_DESC scd{};
    scd.BufferCount = 2;
    scd.BufferDesc.Width = g_win_w; scd.BufferDesc.Height = g_win_h;
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferDesc.RefreshRate.Numerator = 60; scd.BufferDesc.RefreshRate.Denominator = 1;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow = hwnd;
    scd.SampleDesc.Count = 1;
    scd.Windowed = TRUE;
    scd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0};
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        levels, 1, D3D11_SDK_VERSION, &scd, &g_swap, &g_dev, nullptr, &g_ctx);
    if (FAILED(hr)) return false;

    ID3D11Texture2D* back = nullptr;
    g_swap->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&back);
    g_dev->CreateRenderTargetView(back, nullptr, &g_rtv);
    back->Release();

    D3D11_TEXTURE2D_DESC dd{};
    dd.Width = g_win_w; dd.Height = g_win_h; dd.MipLevels = 1; dd.ArraySize = 1;
    dd.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; dd.SampleDesc.Count = 1;
    dd.Usage = D3D11_USAGE_DEFAULT; dd.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    ID3D11Texture2D* depth = nullptr;
    g_dev->CreateTexture2D(&dd, nullptr, &depth);
    g_dev->CreateDepthStencilView(depth, nullptr, &g_dsv);
    depth->Release();

    g_ctx->OMSetRenderTargets(1, &g_rtv, g_dsv);

    D3D11_VIEWPORT vp{};
    vp.Width = (float)g_win_w; vp.Height = (float)g_win_h;
    vp.MinDepth = 0; vp.MaxDepth = 1;
    g_ctx->RSSetViewports(1, &vp);
    return create_shaders();
}

bool load_model(const bmd::Model& model, int mode) {
    if (!g_dev) return false;
    g_render_mode = mode;
    std::vector<PVertex> verts;
    std::vector<uint32_t> idx;

    // Bone skinning transform (validated SM64DSe math).
    // vertex.matrix_id -> group.bone_ids[matrix_id] -> bone id -> bone.world
    // position: v' = v * W  (row-vector); normal: n' = n * R (rotation part, no translation)
    auto apply_bone = [&](const bmd::Vertex& v, const bmd::MaterialGroup& g,
                          float& px, float& py, float& pz,
                          float& nx, float& ny, float& nz) {
        int bone_id = 0;
        if (v.matrix_id >= 0 && v.matrix_id < (int)g.bone_ids.size())
            bone_id = g.bone_ids[v.matrix_id];
        if (bone_id < 0 || bone_id >= (int)model.bones.size()) bone_id = 0;
        const float* W = model.bones[bone_id].world;
        px = v.x*W[0] + v.y*W[4] + v.z*W[8]  + W[12];
        py = v.x*W[1] + v.y*W[5] + v.z*W[9]  + W[13];
        pz = v.x*W[2] + v.y*W[6] + v.z*W[10] + W[14];
        nx = v.nx*W[0] + v.ny*W[4] + v.nz*W[8];
        ny = v.nx*W[1] + v.ny*W[5] + v.nz*W[9];
        nz = v.nx*W[2] + v.ny*W[6] + v.nz*W[10];
    };

    // compute bbox from transformed (world) positions -> camera framing
    float minx=1e30f,miny=1e30f,minz=1e30f,maxx=-1e30f,maxy=-1e30f,maxz=-1e30f;
    bool any=false;
    for (auto& g : model.groups) for (auto& p : g.prims) for (auto& v : p.verts) {
        float px,py,pz,nx,ny,nz; apply_bone(v,g,px,py,pz,nx,ny,nz);
        minx=std::min(minx,px); maxx=std::max(maxx,px);
        miny=std::min(miny,py); maxy=std::max(maxy,py);
        minz=std::min(minz,pz); maxz=std::max(maxz,pz); any=true;
    }
    float radius = 1.0f;
    if (any) {
        g_center = XMFLOAT3((minx+maxx)/2,(miny+maxy)/2,(minz+maxz)/2);
        float dx=(maxx-minx),dy=(maxy-miny),dz=(maxz-minz);
        radius = std::sqrt(dx*dx+dy*dy+dz*dz)*0.5f;
        if (radius < 0.001f) radius = 1.0f;
        g_dist = radius * 3.0f;
    } else {
        g_center = {0,0,0};
    }

    for (auto& g : model.groups) {
        for (auto& p : g.prims) {
            uint32_t base = (uint32_t)verts.size();
            for (auto& v : p.verts) {
                float px,py,pz,nx,ny,nz; apply_bone(v,g,px,py,pz,nx,ny,nz);
                verts.push_back({px, py, pz, nx, ny, nz});
            }
            uint32_t n = (uint32_t)p.verts.size();
            auto add = [&](uint32_t a, uint32_t b, uint32_t c) { idx.push_back(base+a); idx.push_back(base+b); idx.push_back(base+c); };
            if (p.type == bmd::PrimType::Triangles)
                for (uint32_t i=0;i+2<n;i+=3) add(i,i+1,i+2);
            else if (p.type == bmd::PrimType::TriangleStrip)
                for (uint32_t i=0;i+2<n;i++) { if(i&1) add(i+1,i,i+2); else add(i,i+1,i+2); } // alternate winding
            else if (p.type == bmd::PrimType::Quads)
                for (uint32_t i=0;i+3<n;i+=4) { add(i,i+1,i+2); add(i,i+2,i+3); }
            else if (p.type == bmd::PrimType::QuadStrip)
                for (uint32_t i=2;i+1<n;i+=2) { add(i-2,i,i+1); add(i-2,i+1,i-1); }
        }
    }
    if (verts.empty()) return false;
    g_model_vcount=(UINT)verts.size(); g_model_icount=(UINT)idx.size();

    D3D11_BUFFER_DESC vbd{};
    vbd.ByteWidth=(UINT)(verts.size()*sizeof(PVertex));
    vbd.Usage=D3D11_USAGE_IMMUTABLE; vbd.BindFlags=D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA vsd{verts.data(),0,0};
    if (FAILED(g_dev->CreateBuffer(&vbd,&vsd,&g_model_vb))) return false;
    D3D11_BUFFER_DESC ibd{};
    ibd.ByteWidth=(UINT)(idx.size()*sizeof(uint32_t));
    ibd.Usage=D3D11_USAGE_IMMUTABLE; ibd.BindFlags=D3D11_BIND_INDEX_BUFFER;
    D3D11_SUBRESOURCE_DATA isd{idx.data(),0,0};
    if (FAILED(g_dev->CreateBuffer(&ibd,&isd,&g_model_ib))) return false;

    make_debug_geometry();
    focus_model();
    return true;
}

void focus_model() {
    // eye positioned to frame g_center at g_dist
    g_dist = std::max(1.0f, g_dist);
    g_yaw = 0.6f; g_pitch = 0.3f;
    make_debug_geometry();
}

void render() {
    if (!g_ctx || !g_dev) return;
    float clear[4] = {0.12f,0.12f,0.15f,1.0f};
    g_ctx->ClearRenderTargetView(g_rtv, clear);
    g_ctx->ClearDepthStencilView(g_dsv, D3D11_CLEAR_DEPTH, 1.0f, 0);

    float ex = std::sin(g_yaw)*std::cos(g_pitch);
    float ey = std::sin(g_pitch);
    float ez = std::cos(g_yaw)*std::cos(g_pitch);
    XMVECTOR eye = XMLoadFloat3(&g_center) + XMVectorScale(XMVectorSet(ex,ey,ez,0), g_dist);
    XMMATRIX V = XMMatrixLookAtLH(eye, XMLoadFloat3(&g_center), XMVectorSet(0,1,0,0));
    XMMATRIX P = XMMatrixPerspectiveFovLH(g_fovy, (float)g_win_w/(float)g_win_h, 0.1f, g_dist*10.0f+1.0f);
    XMMATRIX W = XMMatrixIdentity();

    UINT stride = sizeof(PVertex), off = 0;
    g_ctx->IASetVertexBuffers(0, 1, &g_model_vb, &stride, &off);
    g_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_ctx->IASetInputLayout(g_layout);
    g_ctx->VSSetShader(g_vs, nullptr, 0);
    g_ctx->PSSetShader(g_ps, nullptr, 0);
    g_ctx->VSSetConstantBuffers(0, 1, &g_cb);
    g_ctx->PSSetConstantBuffers(0, 1, &g_cb);

    CB cb; cb.W=W; cb.V=V; cb.P=P;

    // solid model
    if (g_render_mode == 1 || g_render_mode == 2) {
        cb.color = XMFLOAT4(0.85f,0.85f,1.0f,0.0f);
        D3D11_MAPPED_SUBRESOURCE map; g_ctx->Map(g_cb,0,D3D11_MAP_WRITE_DISCARD,0,&map);
        std::memcpy(map.pData,&cb,sizeof(CB)); g_ctx->Unmap(g_cb,0);
        D3D11_RASTERIZER_DESC rd{}; rd.FillMode=D3D11_FILL_SOLID; rd.CullMode=D3D11_CULL_NONE;
        ID3D11RasterizerState* rs=nullptr; g_dev->CreateRasterizerState(&rd,&rs); g_ctx->RSSetState(rs);
        g_ctx->IASetIndexBuffer(g_model_ib, DXGI_FORMAT_R32_UINT, 0);
        g_ctx->DrawIndexed(g_model_icount,0,0);
        if(rs) rs->Release();
    }
    // wireframe model
    if (g_render_mode == 0 || g_render_mode == 2) {
        cb.color = XMFLOAT4(0.9f,0.5f,0.3f,0.0f);
        D3D11_MAPPED_SUBRESOURCE map; g_ctx->Map(g_cb,0,D3D11_MAP_WRITE_DISCARD,0,&map);
        std::memcpy(map.pData,&cb,sizeof(CB)); g_ctx->Unmap(g_cb,0);
        D3D11_RASTERIZER_DESC rd{}; rd.FillMode=D3D11_FILL_WIREFRAME; rd.CullMode=D3D11_CULL_NONE;
        ID3D11RasterizerState* rs=nullptr; g_dev->CreateRasterizerState(&rd,&rs); g_ctx->RSSetState(rs);
        g_ctx->IASetIndexBuffer(g_model_ib, DXGI_FORMAT_R32_UINT, 0);
        g_ctx->DrawIndexed(g_model_icount,0,0);
        if(rs) rs->Release();
    }

    // debug axes + bbox (green lines, unlit)
    if (g_dbg_vb) {
        cb.color = XMFLOAT4(0.2f,0.9f,0.2f,1.0f); // a=1 -> unlit
        D3D11_MAPPED_SUBRESOURCE map; g_ctx->Map(g_cb,0,D3D11_MAP_WRITE_DISCARD,0,&map);
        std::memcpy(map.pData,&cb,sizeof(CB)); g_ctx->Unmap(g_cb,0);
        D3D11_RASTERIZER_DESC rd{}; rd.FillMode=D3D11_FILL_WIREFRAME; rd.CullMode=D3D11_CULL_NONE;
        ID3D11RasterizerState* rs=nullptr; g_dev->CreateRasterizerState(&rd,&rs); g_ctx->RSSetState(rs);
        g_ctx->IASetVertexBuffers(0,1,&g_dbg_vb,&stride,&off);
        g_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
        g_ctx->Draw(g_dbg_vcount,0);
        if(rs) rs->Release();
    }

    if (g_save_path) { save_frame_bmp(g_save_path); g_save_path = nullptr; }
    g_swap->Present(1,0);
}

void set_save_path(const char* path) { g_save_path = path; }

bool save_frame_bmp(const char* path) {
    if (!g_dev || !g_swap || !g_ctx) return false;
    ID3D11Texture2D* back=nullptr;
    if (FAILED(g_swap->GetBuffer(0,__uuidof(ID3D11Texture2D),(void**)&back))) return false;
    D3D11_TEXTURE2D_DESC bd; back->GetDesc(&bd);
    D3D11_TEXTURE2D_DESC sd=bd;
    sd.Usage=D3D11_USAGE_STAGING; sd.CPUAccessFlags=D3D11_CPU_ACCESS_READ; sd.BindFlags=0;
    ID3D11Texture2D* staging=nullptr;
    if (FAILED(g_dev->CreateTexture2D(&sd,nullptr,&staging))){back->Release();return false;}
    g_ctx->CopyResource(staging,back);
    D3D11_MAPPED_SUBRESOURCE map;
    if (FAILED(g_ctx->Map(staging,0,D3D11_MAP_READ,0,&map))){staging->Release();back->Release();return false;}
    int w=(int)bd.Width,h=(int)bd.Height;
    int rowbytes=w*3; int pad=(4-(rowbytes%4))%4;
    std::vector<uint8_t> img((size_t)(rowbytes+pad)*h);
    uint8_t* src=(uint8_t*)map.pData;
    for(int y=0;y<h;y++){
        uint8_t* dst=&img[(size_t)y*(rowbytes+pad)];
        uint8_t* s=src+(size_t)y*map.RowPitch;
        for(int x=0;x<w;x++){ dst[x*3+0]=s[x*4+2]; dst[x*3+1]=s[x*4+1]; dst[x*3+2]=s[x*4+0]; }
    }
    g_ctx->Unmap(staging,0); staging->Release(); back->Release();
    uint32_t data_size=(uint32_t)((rowbytes+pad)*h);
    std::ofstream f(path,std::ios::binary);
    if(!f) return false;
    uint32_t file_size=54+data_size, hdrsize=54, infosize=40; uint16_t planes=1,bpp=24;
    uint8_t hdr[54]={}; hdr[0]='B';hdr[1]='M';
    std::memcpy(hdr+2,&file_size,4); std::memcpy(hdr+10,&hdrsize,4); std::memcpy(hdr+14,&infosize,4);
    std::memcpy(hdr+18,&w,4); std::memcpy(hdr+22,&h,4); std::memcpy(hdr+26,&planes,2); std::memcpy(hdr+28,&bpp,2);
    std::memcpy(hdr+34,&data_size,4);
    f.write((char*)hdr,54); f.write((char*)img.data(),(std::streamsize)img.size());
    return true;
}

bool initialized() { return g_dev != nullptr; }
void mouse_drag(int dx,int dy){ g_yaw+=dx*0.01f; g_pitch+=dy*0.01f; g_pitch=std::max(-1.5f,std::min(1.5f,g_pitch)); }
void mouse_wheel(int delta){ g_dist=std::max(0.5f,std::min(500.0f,g_dist-delta*0.02f)); make_debug_geometry(); }
void reset_camera(){ g_yaw=0.6f; g_pitch=0.3f; }

void shutdown() {
    if(g_dbg_vb)g_dbg_vb->Release(); if(g_model_ib)g_model_ib->Release(); if(g_model_vb)g_model_vb->Release();
    if(g_ib)g_ib->Release(); if(g_vb)g_vb->Release(); if(g_cb)g_cb->Release();
    if(g_layout)g_layout->Release(); if(g_ps)g_ps->Release(); if(g_vs)g_vs->Release();
    g_dsv->Release(); g_rtv->Release(); g_swap->Release(); g_ctx->Release(); g_dev->Release();
    g_dbg_vb=nullptr; g_model_ib=nullptr; g_model_vb=nullptr; g_ib=nullptr; g_vb=nullptr; g_cb=nullptr;
    g_layout=nullptr; g_ps=nullptr; g_vs=nullptr; g_dsv=nullptr; g_rtv=nullptr;
    g_swap=nullptr; g_ctx=nullptr; g_dev=nullptr;
}

} // namespace d3dview