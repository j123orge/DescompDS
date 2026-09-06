// DescompDS Explorer — D3D11 model view renderer implementation
#include "d3d_view.h"

#include <d3dcompiler.h>
#include <dxgi.h>
#include <DirectXMath.h>
#include <vector>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <string>

#include "stb_image.h"

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
ID3D11ShaderResourceView* g_model_srv = nullptr;
ID3D11SamplerState* g_model_sampler = nullptr;
struct DrawGroupInfo { uint32_t idxCount=0, idxOffset=0; int texIdx=-1; uint32_t polyAttrib=0; };
std::vector<DrawGroupInfo> g_drawGroups;
std::vector<ID3D11ShaderResourceView*> g_texSRVs;

// debug geometry (axes + bbox): line list
ID3D11Buffer* g_dbg_vb = nullptr;
UINT g_dbg_vcount = 0;

int g_render_mode = 1;
int g_primitive_mode = 0; // 0=ALL, 1=TRIANGLES, 2=QUADS, 3=TRISTRIP, 4=QUADSTRIP
int g_debug_group = -1; // -1 = all groups
const char* g_save_path = nullptr;

// camera
XMFLOAT3 g_center = {0, 0, 0};
float g_yaw = 0.6f, g_pitch = 0.3f, g_dist = 12.0f;
float g_fovy = 0.7f;
int g_win_w = 800, g_win_h = 600;

struct CB { XMMATRIX W; XMMATRIX V; XMMATRIX P; XMFLOAT4 color; };

struct PVertex { float x, y, z; float nx, ny, nz; float u, v; };

const char* k_vs_src =
    "cbuffer CB : register(b0) { row_major float4x4 W; row_major float4x4 V; row_major float4x4 P; float4 Color; };\n"
    "struct VIn { float3 p : POS; float3 n : NORMAL; float2 t : TEXCOORD; };\n"
    "struct VOut { float4 p : SV_Position; float3 n : NORMAL; float2 t : TEXCOORD; };\n"
    "VOut main(VIn i) { VOut o; float4 wp = mul(float4(i.p,1), W); "
    "o.p = mul(mul(wp, V), P); o.n = i.n; o.t = i.t; return o; }\n";

const char* k_ps_src =
    "Texture2D tex0 : register(t0); SamplerState samp0 : register(s0);\n"
    "cbuffer CB : register(b0) { row_major float4x4 W; row_major float4x4 V; row_major float4x4 P; float4 Color; };\n"
    "struct VOut { float4 p : SV_Position; float3 n : NORMAL; float2 t : TEXCOORD; };\n"
    "float4 main(VOut i) : SV_Target { "
    "float4 tc = tex0.Sample(samp0, i.t); "
    "if(tc.a < 0.1) discard; "
    "return float4(tc.rgb * Color.rgb, 1.0); }\n";

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
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    g_dev->CreateInputLayout(desc, 3, vsb->GetBufferPointer(), vsb->GetBufferSize(), &g_layout);

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

    // Create textures per model.textures (decoded in bmd_v2)
    for(auto srv: g_texSRVs) if(srv) srv->Release();
    g_texSRVs.assign(model.textures.size(), nullptr);
    g_drawGroups.clear();
    for(size_t ti=0; ti<model.textures.size(); ++ti){
        auto &tex = model.textures[ti];
        if(tex.decoded.empty() || tex.width==0 || tex.height==0) continue;
        D3D11_TEXTURE2D_DESC td{}; td.Width=tex.width; td.Height=tex.height; td.MipLevels=1; td.ArraySize=1;
        td.Format=DXGI_FORMAT_B8G8R8A8_UNORM; td.SampleDesc.Count=1; td.Usage=D3D11_USAGE_IMMUTABLE; td.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA sd{tex.decoded.data(), (UINT)(tex.width*4), (UINT)(tex.width*tex.height*4)};
        ID3D11Texture2D* t2d=nullptr;
        if(SUCCEEDED(g_dev->CreateTexture2D(&td,&sd,&t2d))){
            g_dev->CreateShaderResourceView(t2d,nullptr,&g_texSRVs[ti]);
            t2d->Release();
        }
    }
    for (size_t gi=0; gi<model.groups.size(); ++gi) {
        auto& g = model.groups[gi];
        if(g_debug_group!=-1 && (int)gi != g_debug_group) {
            // Still need to count verts for bbox, but skip index generation for debug
            // For now, still generate but we will filter in render
        }
        uint32_t groupStartIdx = (uint32_t)idx.size();
        // For debug, use white texture for all to isolate geometry vs texture
        int texIdx = -1;
        // if(g.tex_id != 0xFFFFFFFF && g.tex_id < (int)g_texSRVs.size() && g_texSRVs[g.tex_id]) texIdx = (int)g.tex_id;
        DrawGroupInfo dgi; dgi.texIdx=texIdx; dgi.polyAttrib=g.poly_attribs; dgi.idxOffset=groupStartIdx;
        for (auto& p : g.prims) {
            uint32_t base = (uint32_t)verts.size();
            for (auto& v : p.verts) {
                float px,py,pz,nx,ny,nz; apply_bone(v,g,px,py,pz,nx,ny,nz);
                verts.push_back({px, py, pz, nx, ny, nz, v.u, v.v});
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
                for (uint32_t i=2;i+1<n;i+=2) { add(i-2,i-1,i+1); add(i-2,i+1,i); }
        }
        dgi.idxCount = (uint32_t)idx.size() - groupStartIdx;
        g_drawGroups.push_back(dgi);
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

    // Create default white 1x1 texture and sampler (placeholder for per-material textures)
    if(g_model_srv) g_model_srv->Release();
    if(g_model_sampler) g_model_sampler->Release();
    {
        uint32_t white=0xFFFFFFFF;
        D3D11_TEXTURE2D_DESC td{}; td.Width=1; td.Height=1; td.MipLevels=1; td.ArraySize=1;
        td.Format=DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count=1; td.Usage=D3D11_USAGE_IMMUTABLE; td.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA sd{&white,sizeof(uint32_t),sizeof(uint32_t)};
        ID3D11Texture2D* tex=nullptr;
        if(SUCCEEDED(g_dev->CreateTexture2D(&td,&sd,&tex))){
            g_dev->CreateShaderResourceView(tex,nullptr,&g_model_srv);
            tex->Release();
        }
        D3D11_SAMPLER_DESC sd2{}; sd2.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT; sd2.AddressU=D3D11_TEXTURE_ADDRESS_CLAMP; sd2.AddressV=D3D11_TEXTURE_ADDRESS_CLAMP; sd2.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
        sd2.ComparisonFunc=D3D11_COMPARISON_NEVER; sd2.MinLOD=0; sd2.MaxLOD=D3D11_FLOAT32_MAX;
        g_dev->CreateSamplerState(&sd2,&g_model_sampler);
    }

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

// ---- Neutral mesh renderer ----
struct PVertexColor { float x, y, z; float r, g, b, a; float u, v; };
static ID3D11Buffer* g_neutral_vb = nullptr;
static ID3D11Buffer* g_neutral_ib = nullptr;
static UINT g_neutral_vcount = 0, g_neutral_icount = 0;
static ID3D11VertexShader* g_neutral_vs = nullptr;
static ID3D11PixelShader* g_neutral_ps = nullptr;
static ID3D11InputLayout* g_neutral_layout = nullptr;
static std::vector<uint32_t> g_neutral_submesh_starts;
static std::vector<uint32_t> g_neutral_submesh_counts;
static std::vector<int> g_neutral_submesh_matid;
static std::vector<ID3D11ShaderResourceView*> g_neutral_texSRVs;
static ID3D11SamplerState* g_neutral_sampler = nullptr;
static bool g_neutral_loaded = false;

static const char* k_neutral_vs_src =
    "cbuffer CB : register(b0) { row_major float4x4 W; row_major float4x4 V; row_major float4x4 P; float4 Color; };\n"
    "struct VIn { float3 p : POS; float4 c : COLOR; float2 t : TEXCOORD; };\n"
    "struct VOut { float4 p : SV_Position; float4 c : COLOR; float2 t : TEXCOORD; };\n"
    "VOut main(VIn i) { VOut o; float4 wp = mul(float4(i.p,1), W); "
    "o.p = mul(mul(wp, V), P); o.c = i.c; o.t = i.t; return o; }\n";

static const char* k_neutral_ps_src =
    "Texture2D tex0 : register(t0); SamplerState samp0 : register(s0);\n"
    "cbuffer CB : register(b0) { row_major float4x4 W; row_major float4x4 V; row_major float4x4 P; float4 Color; };\n"
    "struct VOut { float4 p : SV_Position; float4 c : COLOR; float2 t : TEXCOORD; };\n"
    "float4 main(VOut i) : SV_Target {\n"
    "  float4 tc = tex0.Sample(samp0, i.t);\n"
    "  if(tc.a < 0.1) discard;\n"
    "  return float4(tc.rgb * i.c.rgb * Color.rgb, 1.0);\n"
    "}\n";

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
    // Select primitive topology based on debug mode
    D3D11_PRIMITIVE_TOPOLOGY topo = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    if (g_primitive_mode == 1) topo = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    else if (g_primitive_mode == 2) topo = (D3D11_PRIMITIVE_TOPOLOGY)8; // QUADLIST (not in D3D11)
    else if (g_primitive_mode == 3) topo = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
    else if (g_primitive_mode == 4) topo = (D3D11_PRIMITIVE_TOPOLOGY)11; // QUADSTRIP (not in D3D11)
    g_ctx->IASetPrimitiveTopology(topo);
    g_ctx->IASetInputLayout(g_layout);
    g_ctx->VSSetShader(g_vs, nullptr, 0);
    g_ctx->PSSetShader(g_ps, nullptr, 0);
    if(g_model_srv) g_ctx->PSSetShaderResources(0,1,&g_model_srv);
    if(g_model_sampler) g_ctx->PSSetSamplers(0,1,&g_model_sampler);
    g_ctx->VSSetConstantBuffers(0, 1, &g_cb);
    g_ctx->PSSetConstantBuffers(0, 1, &g_cb);

    CB cb; cb.W=W; cb.V=V; cb.P=P;

    // solid model — per-group with texture and culling (SM64DSe BMD.cs:540)
    if (g_neutral_loaded && g_neutral_vb && g_neutral_ib) {
        // Neutral mesh rendering (textured)
        UINT stride = sizeof(PVertexColor), off = 0;
        g_ctx->IASetVertexBuffers(0, 1, &g_neutral_vb, &stride, &off);
        g_ctx->IASetIndexBuffer(g_neutral_ib, DXGI_FORMAT_R32_UINT, 0);
        g_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        g_ctx->IASetInputLayout(g_neutral_layout);
        g_ctx->VSSetShader(g_neutral_vs, nullptr, 0);
        g_ctx->PSSetShader(g_neutral_ps, nullptr, 0);
        g_ctx->VSSetConstantBuffers(0, 1, &g_cb);
        g_ctx->PSSetConstantBuffers(0, 1, &g_cb);
        if (g_neutral_sampler) g_ctx->PSSetSamplers(0, 1, &g_neutral_sampler);

        cb.color = XMFLOAT4(1, 1, 1, 1);

        D3D11_RASTERIZER_DESC rd{}; rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_NONE;
        rd.FrontCounterClockwise = TRUE;
        ID3D11RasterizerState* rs = nullptr; g_dev->CreateRasterizerState(&rd, &rs); g_ctx->RSSetState(rs);

        for (size_t si = 0; si < g_neutral_submesh_starts.size(); si++) {
            int matid = (si < g_neutral_submesh_matid.size()) ? g_neutral_submesh_matid[si] : -1;
            ID3D11ShaderResourceView* srv = nullptr;
            if (matid >= 0 && matid < (int)g_neutral_texSRVs.size() && g_neutral_texSRVs[matid])
                srv = g_neutral_texSRVs[matid];
            g_ctx->PSSetShaderResources(0, 1, &srv);

            D3D11_MAPPED_SUBRESOURCE map; g_ctx->Map(g_cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &map);
            std::memcpy(map.pData, &cb, sizeof(CB)); g_ctx->Unmap(g_cb, 0);

            g_ctx->DrawIndexed(g_neutral_submesh_counts[si], g_neutral_submesh_starts[si], 0);
        }
        if (rs) rs->Release();
    } else if (g_render_mode == 1 || g_render_mode == 2) {
        g_ctx->IASetIndexBuffer(g_model_ib, DXGI_FORMAT_R32_UINT, 0);
        for(size_t gi=0; gi<g_drawGroups.size(); ++gi){
            auto &dg = g_drawGroups[gi];
            if(g_debug_group!=-1 && (int)gi != g_debug_group) continue;
            // Cull per poly_attrib — for debug, disable all culling
            D3D11_CULL_MODE cull=D3D11_CULL_NONE;
            // uint32_t ca=dg.polyAttrib & 0xC0;
            // if(ca==0x40) cull=D3D11_CULL_FRONT;
            // else if(ca==0x80) cull=D3D11_CULL_BACK;
            // else if(ca==0x00) continue; // FrontAndBack — cull both => skip
            // Texture per group
            ID3D11ShaderResourceView* srv=nullptr;
            if(dg.texIdx>=0 && dg.texIdx < (int)g_texSRVs.size() && g_texSRVs[dg.texIdx]) srv=g_texSRVs[dg.texIdx];
            else srv=g_model_srv;
            g_ctx->PSSetShaderResources(0,1,&srv);
            if(g_model_sampler) g_ctx->PSSetSamplers(0,1,&g_model_sampler);
            cb.color = XMFLOAT4(1,1,1,0);
            D3D11_MAPPED_SUBRESOURCE map; g_ctx->Map(g_cb,0,D3D11_MAP_WRITE_DISCARD,0,&map);
            std::memcpy(map.pData,&cb,sizeof(CB)); g_ctx->Unmap(g_cb,0);
            D3D11_RASTERIZER_DESC rd{}; rd.FillMode=D3D11_FILL_SOLID; rd.CullMode=cull; rd.FrontCounterClockwise=TRUE;
            ID3D11RasterizerState* rs=nullptr; g_dev->CreateRasterizerState(&rd,&rs); g_ctx->RSSetState(rs);
            g_ctx->DrawIndexed(dg.idxCount, dg.idxOffset, 0);
            if(rs) rs->Release();
        }
    }
    // wireframe model
    if (g_render_mode == 0 || g_render_mode == 2) {
        cb.color = XMFLOAT4(0.9f,0.5f,0.3f,0.0f);
        D3D11_MAPPED_SUBRESOURCE map; g_ctx->Map(g_cb,0,D3D11_MAP_WRITE_DISCARD,0,&map);
        std::memcpy(map.pData,&cb,sizeof(CB)); g_ctx->Unmap(g_cb,0);
        D3D11_RASTERIZER_DESC rd{}; rd.FillMode=D3D11_FILL_WIREFRAME; rd.CullMode=D3D11_CULL_NONE; rd.FrontCounterClockwise=TRUE;
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
void set_debug_group(int idx){ g_debug_group=idx; }
int get_debug_group(){ return g_debug_group; }
int get_primitive_mode(){ return g_primitive_mode; }
void set_primitive_mode(int mode){ g_primitive_mode = mode; }
void mouse_drag(int dx,int dy){ g_yaw+=dx*0.01f; g_pitch+=dy*0.01f; g_pitch=std::max(-1.5f,std::min(1.5f,g_pitch)); }
void mouse_wheel(int delta){ g_dist=std::max(0.5f,std::min(500.0f,g_dist-delta*0.02f)); make_debug_geometry(); }
void reset_camera(){ g_yaw=0.6f; g_pitch=0.3f; }

bool load_neutral_mesh(const NeutralMesh& mesh, int mode) {
    if (!g_dev || mesh.empty()) return false;

    // Release old neutral mesh resources
    if (g_neutral_vb) { g_neutral_vb->Release(); g_neutral_vb = nullptr; }
    if (g_neutral_ib) { g_neutral_ib->Release(); g_neutral_ib = nullptr; }
    g_neutral_submesh_starts.clear();
    g_neutral_submesh_counts.clear();
    g_neutral_loaded = false;

    // Create neutral shaders if not yet created
    if (!g_neutral_vs) {
        ID3DBlob* vsb = nullptr; ID3DBlob* psb = nullptr; ID3DBlob* err = nullptr;
        HRESULT hr = D3DCompile(k_neutral_vs_src, strlen(k_neutral_vs_src), nullptr, nullptr, nullptr,
                                "main", "vs_4_0", 0, 0, &vsb, &err);
        if (FAILED(hr)) { if (err) err->Release(); return false; }
        hr = D3DCompile(k_neutral_ps_src, strlen(k_neutral_ps_src), nullptr, nullptr, nullptr,
                        "main", "ps_4_0", 0, 0, &psb, &err);
        if (FAILED(hr)) { if (err) err->Release(); return false; }
        g_dev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &g_neutral_vs);
        g_dev->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &g_neutral_ps);
        D3D11_INPUT_ELEMENT_DESC desc[] = {
            {"POS", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 28, D3D11_INPUT_PER_VERTEX_DATA, 0},
        };
        g_dev->CreateInputLayout(desc, 3, vsb->GetBufferPointer(), vsb->GetBufferSize(), &g_neutral_layout);
    }

    // Convert NeutralMesh to GPU buffers
    std::vector<PVertexColor> verts;
    verts.reserve(mesh.vertices.size());
    for (auto& v : mesh.vertices) {
        PVertexColor pv;
        pv.x = v.pos[0]; pv.y = v.pos[1]; pv.z = v.pos[2];
        pv.r = v.color[0] / 255.0f;
        pv.g = v.color[1] / 255.0f;
        pv.b = v.color[2] / 255.0f;
        pv.a = v.color[3] / 255.0f;
        pv.u = v.uv[0]; pv.v = v.uv[1];
        verts.push_back(pv);
    }
    std::vector<uint32_t> indices = mesh.indices;

    g_neutral_vcount = (UINT)verts.size();
    g_neutral_icount = (UINT)indices.size();

    D3D11_BUFFER_DESC vbd{};
    vbd.ByteWidth = (UINT)(verts.size() * sizeof(PVertexColor));
    vbd.Usage = D3D11_USAGE_IMMUTABLE; vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA vsd{verts.data(), 0, 0};
    if (FAILED(g_dev->CreateBuffer(&vbd, &vsd, &g_neutral_vb))) return false;

    D3D11_BUFFER_DESC ibd{};
    ibd.ByteWidth = (UINT)(indices.size() * sizeof(uint32_t));
    ibd.Usage = D3D11_USAGE_IMMUTABLE; ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    D3D11_SUBRESOURCE_DATA isd{indices.data(), 0, 0};
    if (FAILED(g_dev->CreateBuffer(&ibd, &isd, &g_neutral_ib))) return false;

    // Store submesh ranges and material IDs
    for (auto& sm : mesh.submeshes) {
        g_neutral_submesh_starts.push_back((uint32_t)sm.index_start);
        g_neutral_submesh_counts.push_back((uint32_t)sm.index_count);
        g_neutral_submesh_matid.push_back(sm.material_id);
    }

    // Load material textures via stb_image
    for (auto srv : g_neutral_texSRVs) if (srv) srv->Release();
    g_neutral_texSRVs.clear();
    g_neutral_texSRVs.resize(mesh.materials.size(), nullptr);
    for (size_t i = 0; i < mesh.materials.size(); i++) {
        auto& m = mesh.materials[i];
        if (m.diffuse_tex.empty()) continue;
        std::string full = "D:\\DescompDS\\test_export\\" + m.diffuse_tex;
        int w = 0, h = 0, ch = 0;
        unsigned char* pixels = stbi_load(full.c_str(), &w, &h, &ch, 4);
        if (!pixels || w == 0 || h == 0) {
            if (pixels) stbi_image_free(pixels);
            continue;
        }
        D3D11_TEXTURE2D_DESC td{};
        td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_IMMUTABLE; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA sd{pixels, (UINT)(w * 4), (UINT)(w * h * 4)};
        ID3D11Texture2D* t2d = nullptr;
        if (SUCCEEDED(g_dev->CreateTexture2D(&td, &sd, &t2d))) {
            g_dev->CreateShaderResourceView(t2d, nullptr, &g_neutral_texSRVs[i]);
            t2d->Release();
        }
        stbi_image_free(pixels);
    }

    // Create sampler
    if (!g_neutral_sampler) {
        D3D11_SAMPLER_DESC sd{};
        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
        sd.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
        sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
        sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
        sd.MinLOD = 0; sd.MaxLOD = D3D11_FLOAT32_MAX;
        g_dev->CreateSamplerState(&sd, &g_neutral_sampler);
    }

    g_neutral_loaded = true;
    g_render_mode = mode;

    // Frame the model
    float dx = mesh.bbox_max[0] - mesh.bbox_min[0];
    float dy = mesh.bbox_max[1] - mesh.bbox_min[1];
    float dz = mesh.bbox_max[2] - mesh.bbox_min[2];
    g_center = XMFLOAT3(
        (mesh.bbox_min[0] + mesh.bbox_max[0]) * 0.5f,
        (mesh.bbox_min[1] + mesh.bbox_max[1]) * 0.5f,
        (mesh.bbox_min[2] + mesh.bbox_max[2]) * 0.5f);
    float radius = std::sqrt(dx*dx + dy*dy + dz*dz) * 0.5f;
    if (radius < 0.001f) radius = 1.0f;
    g_dist = radius * 3.0f;
    focus_model();

    return true;
}

void shutdown() {
    if(g_neutral_vb)g_neutral_vb->Release(); if(g_neutral_ib)g_neutral_ib->Release();
    if(g_neutral_vs)g_neutral_vs->Release(); if(g_neutral_ps)g_neutral_ps->Release();
    if(g_neutral_layout)g_neutral_layout->Release();
    if(g_dbg_vb)g_dbg_vb->Release(); if(g_model_ib)g_model_ib->Release(); if(g_model_vb)g_model_vb->Release();
    if(g_ib)g_ib->Release(); if(g_vb)g_vb->Release(); if(g_cb)g_cb->Release();
    if(g_layout)g_layout->Release(); if(g_ps)g_ps->Release(); if(g_vs)g_vs->Release();
    g_dsv->Release(); g_rtv->Release(); g_swap->Release(); g_ctx->Release(); g_dev->Release();
    g_neutral_vb=nullptr; g_neutral_ib=nullptr; g_neutral_vs=nullptr; g_neutral_ps=nullptr; g_neutral_layout=nullptr;
    g_dbg_vb=nullptr; g_model_ib=nullptr; g_model_vb=nullptr; g_ib=nullptr; g_vb=nullptr; g_cb=nullptr;
    g_layout=nullptr; g_ps=nullptr; g_vs=nullptr; g_dsv=nullptr; g_rtv=nullptr;
    g_swap=nullptr; g_ctx=nullptr; g_dev=nullptr;
}

} // namespace d3dview