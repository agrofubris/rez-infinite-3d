#include "reticle_pos.h"

#include <d3dcompiler.h>
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>

#include "log.h"
#include "reticle_vs.h"

namespace {

std::mutex g_lock;
bool g_failed = false;  // gave up after an error (logged once)
ID3D11GeometryShader* g_gs = nullptr;
ID3D11PixelShader* g_ps = nullptr;
ID3D11RasterizerState* g_raster = nullptr;
ID3D11DepthStencilState* g_noDepth = nullptr;
float g_tanH = 1.0f, g_tanV = 1.0f, g_shiftV = 0.0f;
LARGE_INTEGER g_freq{};
int g_sequence = 0;
const int kRing = 4;     // results are read this many measurements later at most
const int kValues = 5;   // centre x, y, half x, y (NDC), clip w

struct EyeState {
    ID3D11Buffer* result = nullptr;
    ID3D11UnorderedAccessView* uav = nullptr;
    ID3D11Buffer* staging[kRing] = {};
    LARGE_INTEGER issued[kRing] = {};
    float shift[kRing] = {};
    bool pending[kRing] = {};
    int next = 0;
    bool have = false;
    LARGE_INTEGER drawn{};
    float x = 0, y = 0, hx = 0, hy = 0;
    float gameX = 0;  // x where the game draws it, before our reticle depth shift
    unsigned measured = 0;
    double lagSum = 0;
    unsigned lagCount = 0;
};
EyeState g_eye[2];

void Fail(const char* what, HRESULT hr) {
    g_failed = true;
    LogPrintf("Reticle position: %s failed (0x%08lX), keeping the reticle in view is off", what, (unsigned long)hr);
}

ID3DBlob* Compile(const std::string& src, const char* target) {
    ID3DBlob* code = nullptr;
    ID3DBlob* errors = nullptr;
    HRESULT hr = D3DCompile(src.c_str(), src.size(), "reticle_pos", nullptr, nullptr, "main", target,
                            D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
    if (FAILED(hr)) {
        LogPrintf("Reticle position: shader source:\n%s", src.c_str());
        if (errors) LogPrintf("Reticle position: %s", (const char*)errors->GetBufferPointer());
        Fail(target, hr);
    }
    if (errors) errors->Release();
    return SUCCEEDED(hr) ? code : nullptr;
}

// The geometry shader redraws the quad as it is shown (RsReshape: flat and/or scaled), finds
// its centre (the middle of a triangle's longest side, the quad's diagonal) and half-size in
// NDC, and hands them to a small triangle in the middle of the view, so they reach our pixel
// shader wherever the quad is, even off screen.
bool BuildShaders(ID3D11Device* dev) {
    std::string fields, pos, uv, error;
    if (!ReticleVSFields(fields, pos, uv, error)) {
        Fail(error.c_str(), E_FAIL);
        return false;
    }
    std::string gs =
        "struct V {\n" + fields +
        "};\n"
        "struct O {\n"
        "    float4 pos : SV_Position;\n"
        "    nointerpolation float4 c : TEXCOORD0;\n"
        "    nointerpolation float2 h : TEXCOORD1;\n"
        "};\n" +
        std::string(ReticleShapeHLSL()) +
        "[maxvertexcount(3)]\n"
        "void main(triangle V i[3], inout TriangleStream<O> o) {\n"
        "    float4 p0 = i[0].POS, p1 = i[1].POS, p2 = i[2].POS;\n"
        "    float2 t0 = " + ReticleUV(uv, "i[0]") + ", t1 = " + ReticleUV(uv, "i[1]") +
        ", t2 = " + ReticleUV(uv, "i[2]") +
        ";\n"
        "    RsReshape(p0, p1, p2, t0, t1, t2);\n"
        "    float2 s0 = p0.xy / p0.w, s1 = p1.xy / p1.w, s2 = p2.xy / p2.w;\n"
        "    float4 c = RsCenter(p0, p1, p2, t0, t1, t2);\n"
        "    float2 cn = c.xy / c.w;\n"
        "    O v;\n"
        "    v.c = float4(cn, c.w, 1);\n"
        "    v.h = max(abs(s0 - cn), max(abs(s1 - cn), abs(s2 - cn)));\n"
        "    v.pos = float4(-0.02, -0.02, 0.5, 1); o.Append(v);\n"
        "    v.pos = float4(0.06, -0.02, 0.5, 1); o.Append(v);\n"
        "    v.pos = float4(-0.02, 0.06, 0.5, 1); o.Append(v);\n"
        "}\n";
    for (size_t at; (at = gs.find("POS")) != std::string::npos;) gs.replace(at, 3, pos);
    const std::string ps =
        "struct O {\n"
        "    float4 pos : SV_Position;\n"
        "    nointerpolation float4 c : TEXCOORD0;\n"
        "    nointerpolation float2 h : TEXCOORD1;\n"
        "};\n"
        "RWStructuredBuffer<uint> result : register(u0);\n"
        "void main(O i) {\n"
        "    result[0] = asuint(i.c.x);\n"
        "    result[1] = asuint(i.c.y);\n"
        "    InterlockedMax(result[2], asuint(i.h.x));\n"  // the larger of both triangles' (floats >= 0 sort as uints)
        "    InterlockedMax(result[3], asuint(i.h.y));\n"
        "    result[4] = asuint(i.c.z);\n"
        "}\n";
    ID3DBlob* gsCode = Compile(gs, "gs_5_0");
    if (!gsCode) return false;
    HRESULT hr = dev->CreateGeometryShader(gsCode->GetBufferPointer(), gsCode->GetBufferSize(), nullptr, &g_gs);
    gsCode->Release();
    if (FAILED(hr)) {
        Fail("creating our geometry shader", hr);
        return false;
    }
    ID3DBlob* psCode = Compile(ps, "ps_5_0");
    if (!psCode) return false;
    hr = dev->CreatePixelShader(psCode->GetBufferPointer(), psCode->GetBufferSize(), nullptr, &g_ps);
    psCode->Release();
    if (FAILED(hr)) {
        Fail("creating our pixel shader", hr);
        return false;
    }
    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.DepthClipEnable = TRUE;
    hr = dev->CreateRasterizerState(&rd, &g_raster);
    if (FAILED(hr)) {
        Fail("creating a rasterizer state", hr);
        return false;
    }
    D3D11_DEPTH_STENCIL_DESC dd{};
    dd.DepthEnable = FALSE;
    dd.DepthFunc = D3D11_COMPARISON_ALWAYS;
    hr = dev->CreateDepthStencilState(&dd, &g_noDepth);
    if (FAILED(hr)) {
        Fail("creating a depth state", hr);
        return false;
    }
    QueryPerformanceFrequency(&g_freq);
    LogPrintf("Reticle position: shaders ready");
    return true;
}

bool PrepareEye(ID3D11Device* dev, EyeState& e) {
    if (e.result) return true;
    D3D11_BUFFER_DESC b{};
    b.ByteWidth = kValues * sizeof(UINT);
    b.Usage = D3D11_USAGE_DEFAULT;
    b.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    b.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    b.StructureByteStride = sizeof(UINT);
    HRESULT hr = dev->CreateBuffer(&b, nullptr, &e.result);
    if (SUCCEEDED(hr)) {
        D3D11_UNORDERED_ACCESS_VIEW_DESC u{};
        u.Format = DXGI_FORMAT_UNKNOWN;
        u.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        u.Buffer.NumElements = kValues;
        hr = dev->CreateUnorderedAccessView(e.result, &u, &e.uav);
    }
    for (int i = 0; SUCCEEDED(hr) && i < kRing; i++) {
        D3D11_BUFFER_DESC s{};
        s.ByteWidth = kValues * sizeof(float);
        s.Usage = D3D11_USAGE_STAGING;
        s.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        hr = dev->CreateBuffer(&s, nullptr, &e.staging[i]);
    }
    if (FAILED(hr)) {
        Fail("creating the result buffers", hr);
        return false;
    }
    return true;
}

// Picks up results that have come back, without waiting
void Collect(ID3D11DeviceContext* ctx, EyeState& e) {
    for (int k = 1; k <= kRing; k++) {
        const int i = (e.next + k) % kRing;  // oldest first
        if (!e.pending[i]) continue;
        D3D11_MAPPED_SUBRESOURCE m{};
        if (ctx->Map(e.staging[i], 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &m) != S_OK) continue;
        float v[kValues];
        memcpy(v, m.pData, sizeof(v));
        ctx->Unmap(e.staging[i], 0);
        e.pending[i] = false;
        if (!(v[4] > 0.0f)) continue;  // behind the eye: no position
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        e.lagSum += (double)(now.QuadPart - e.issued[i].QuadPart) / (double)g_freq.QuadPart;
        e.lagCount++;
        e.gameX = v[0];
        e.x = v[0] + e.shift[i];
        e.y = v[1];
        e.hx = v[2];
        e.hy = v[3];
        e.drawn = e.issued[i];
        e.have = true;
        g_sequence++;
    }
}

}  // namespace

void ReticlePosSetProjection(float tanH, float tanV, float shiftV) {
    std::lock_guard<std::mutex> lk(g_lock);
    g_tanH = tanH;
    g_tanV = tanV;
    g_shiftV = shiftV;
}

bool ReticlePosAim(int eye, float& x, float& y, float& halfX, float& halfY) {
    if (eye < 0 || eye > 1) return false;
    std::lock_guard<std::mutex> lk(g_lock);
    const EyeState& e = g_eye[eye];
    if (!e.have) return false;
    x = e.gameX;
    y = e.y;
    halfX = e.hx;
    halfY = e.hy;
    return true;
}

void ReticlePosProjection(float& tanH, float& tanV, float& shiftV) {
    std::lock_guard<std::mutex> lk(g_lock);
    tanH = g_tanH;
    tanV = g_tanV;
    shiftV = g_shiftV;
}

void ReticlePosMeasure(ID3D11DeviceContext* ctx, int eye, float shiftNdc, const ReticleShape& shape,
                       const std::function<void()>& draw) {
    if (eye < 0 || eye > 1) return;
    std::lock_guard<std::mutex> lk(g_lock);
    if (g_failed) return;
    ID3D11Device* dev = nullptr;
    ctx->GetDevice(&dev);
    const bool ok = dev && (g_gs || BuildShaders(dev)) && PrepareEye(dev, g_eye[eye]);
    if (dev) dev->Release();
    if (!ok) return;
    EyeState& e = g_eye[eye];
    Collect(ctx, e);
    ID3D11Buffer* cb = nullptr;
    if (!ReticleShapeBind(ctx, shape, &cb)) return;

    // Save what we change
    ID3D11GeometryShader* gs = nullptr;
    ID3D11ClassInstance* gsInst[256];
    UINT gsN = 256;
    ctx->GSGetShader(&gs, gsInst, &gsN);
    ID3D11PixelShader* ps = nullptr;
    ID3D11ClassInstance* psInst[256];
    UINT psN = 256;
    ctx->PSGetShader(&ps, psInst, &psN);
    ID3D11RenderTargetView* rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
    ID3D11DepthStencilView* dsv = nullptr;
    ctx->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtvs, &dsv);
    ID3D11DepthStencilState* dss = nullptr;
    UINT stencilRef = 0;
    ctx->OMGetDepthStencilState(&dss, &stencilRef);
    ID3D11RasterizerState* rs = nullptr;
    ctx->RSGetState(&rs);

    // The quad again, into our result instead of the picture
    const UINT zero[4] = {};
    ctx->ClearUnorderedAccessViewUint(e.uav, zero);
    ctx->GSSetShader(g_gs, nullptr, 0);
    ctx->PSSetShader(g_ps, nullptr, 0);
    ctx->OMSetRenderTargetsAndUnorderedAccessViews(0, nullptr, nullptr, 0, 1, &e.uav, nullptr);
    ctx->OMSetDepthStencilState(g_noDepth, 0);
    ctx->RSSetState(g_raster);
    draw();

    // Restore
    ctx->RSSetState(rs);
    ctx->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtvs, dsv);
    ctx->OMSetDepthStencilState(dss, stencilRef);
    ctx->PSSetShader(ps, psInst, psN);
    ctx->GSSetShader(gs, gsInst, gsN);
    ReticleShapeRestore(ctx, cb);
    for (auto* r : rtvs)
        if (r) r->Release();
    if (dsv) dsv->Release();
    if (dss) dss->Release();
    if (rs) rs->Release();
    if (ps) ps->Release();
    if (gs) gs->Release();
    for (UINT i = 0; i < psN; i++)
        if (psInst[i]) psInst[i]->Release();
    for (UINT i = 0; i < gsN; i++)
        if (gsInst[i]) gsInst[i]->Release();

    ctx->CopyResource(e.staging[e.next], e.result);
    QueryPerformanceCounter(&e.issued[e.next]);
    e.shift[e.next] = shiftNdc;
    e.pending[e.next] = true;
    e.next = (e.next + 1) % kRing;
    if (++e.measured % 240 == 1 && e.have)
        LogPrintf("Reticle position: eye %d at %+.3f, %+.3f (half %.3f x %.3f), read back after %.1f ms", eye, e.x,
                  e.y, e.hx, e.hy, e.lagCount ? 1000.0 * e.lagSum / e.lagCount : 0.0);
}

int ReticlePosLatest(Rez3DReticleState* out) {
    if (!out) return 0;
    std::lock_guard<std::mutex> lk(g_lock);
    if (!g_eye[0].have || !g_eye[1].have) return 0;
    memset(out, 0, sizeof(*out));
    out->version = kRez3DReticleStateVersion;
    out->sequence = g_sequence;
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    const LARGE_INTEGER drawn = g_eye[0].drawn.QuadPart > g_eye[1].drawn.QuadPart ? g_eye[0].drawn : g_eye[1].drawn;
    out->drawnAgo = (double)(now.QuadPart - drawn.QuadPart) / (double)g_freq.QuadPart;
    for (int e = 0; e < 2; e++) {
        out->x[e] = g_eye[e].x;
        out->y[e] = g_eye[e].y;
        out->halfX[e] = g_eye[e].hx;
        out->halfY[e] = g_eye[e].hy;
    }
    out->tanH = g_tanH;
    out->tanV = g_tanV;
    out->shiftV = g_shiftV;
    return 1;
}
