#include "reticle_probe.h"

#include <d3dcompiler.h>
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>

#include "log.h"
#include "reticle_pos.h"
#include "reticle_vs.h"

namespace {

const int kSlots = 256;  // draws recorded per eye per frame
const int kRing = 4;     // results are read this many frames later at most
int g_logEvery = 120;
const float kBox = 0.25f;  // the box: this share of the octagon's half-size, around its middle

std::mutex g_lock;
bool g_failed = false;
ID3D11PixelShader* g_ps[2] = {};  // without, with a texture in slot 0
ID3D11RasterizerState* g_raster = nullptr;
ID3D11DepthStencilState* g_noDepth = nullptr;
ID3D11Buffer* g_cb = nullptr;  // the draw's slot (register b13)

struct Slot {
    uint32_t ps;
    unsigned verts;
};

struct EyeState {
    ID3D11Buffer* result = nullptr;  // per slot: nearest depth (as uint), pixels
    ID3D11UnorderedAccessView* uav = nullptr;
    ID3D11Buffer* staging[kRing] = {};
    Slot slots[kRing][kSlots] = {};
    int counts[kRing] = {};
    bool pending[kRing] = {};
    int next = 0;
    Slot frame[kSlots] = {};  // this frame's draws so far
    int count = 0;
    bool cleared = false;
    bool ended = false;
    float nearest = 0.0f;  // the latest result
    unsigned draws = 0;
    bool have = false;
    unsigned results = 0;
};
EyeState g_eye[2];

void Fail(const char* what, HRESULT hr) {
    g_failed = true;
    LogPrintf("Reticle probe: %s failed (0x%08lX), reticle depth falls back to the depth buffer", what,
              (unsigned long)hr);
}

bool BuildShaders(ID3D11Device* dev) {
    std::string fields, pos, uv, error, color;
    if (!ReticleVSFields(fields, pos, uv, error, &color)) {
        Fail(error.c_str(), E_FAIL);
        return false;
    }
    // Visible = the texture (if any) times the vertex colour, in colour and in alpha: transparent
    // corners of a sprite and black parts of an additive glow don't count
    for (int textured = 0; textured < 2; textured++) {
        std::string src =
            "struct In {\n" + fields +
            "};\n"
            "Texture2D tex : register(t0);\n"
            "SamplerState smp : register(s0);\n"
            "cbuffer Probe : register(b13) { uint slot; uint3 pad; };\n"
            "RWStructuredBuffer<uint> result : register(u0);\n"
            "void main(In i) {\n"
            "    float4 c = " + (color.empty() ? std::string("float4(1, 1, 1, 1)") : color) + ";\n" +
            (textured && !uv.empty() ? "    c *= tex.Sample(smp, " + uv + ");\n" : std::string()) +
            "    float seen = max(max(c.r, c.g), c.b) * c.a;\n"
            "    if (seen < 0.05) return;\n"
            "    InterlockedMax(result[slot * 2], asuint(i." + pos + ".z));\n"
            "    InterlockedAdd(result[slot * 2 + 1], 1);\n"
            "}\n";
        ID3DBlob* code = nullptr;
        ID3DBlob* errors = nullptr;
        HRESULT hr = D3DCompile(src.c_str(), src.size(), "reticle_probe", nullptr, nullptr, "main", "ps_5_0",
                                D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
        if (FAILED(hr)) {
            LogPrintf("Reticle probe: shader source:\n%s", src.c_str());
            if (errors) LogPrintf("Reticle probe: %s", (const char*)errors->GetBufferPointer());
            if (errors) errors->Release();
            Fail("compiling our pixel shader", hr);
            return false;
        }
        if (errors) errors->Release();
        hr = dev->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &g_ps[textured]);
        code->Release();
        if (FAILED(hr)) {
            Fail("creating our pixel shader", hr);
            return false;
        }
    }
    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.DepthClipEnable = TRUE;
    rd.ScissorEnable = TRUE;
    D3D11_DEPTH_STENCIL_DESC dd{};
    dd.DepthEnable = FALSE;
    dd.DepthFunc = D3D11_COMPARISON_ALWAYS;
    D3D11_BUFFER_DESC cb{};
    cb.ByteWidth = 16;
    cb.Usage = D3D11_USAGE_DYNAMIC;
    cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    HRESULT hr = dev->CreateRasterizerState(&rd, &g_raster);
    if (SUCCEEDED(hr)) hr = dev->CreateDepthStencilState(&dd, &g_noDepth);
    if (SUCCEEDED(hr)) hr = dev->CreateBuffer(&cb, nullptr, &g_cb);
    if (FAILED(hr)) {
        Fail("creating our states", hr);
        return false;
    }
    LogPrintf("Reticle probe: shaders ready (%s colour, %s texture coordinates)", color.empty() ? "no" : "with",
              uv.empty() ? "no" : "with");
    return true;
}

bool PrepareEye(ID3D11Device* dev, EyeState& e) {
    if (e.result) return true;
    D3D11_BUFFER_DESC b{};
    b.ByteWidth = kSlots * 2 * sizeof(UINT);
    b.Usage = D3D11_USAGE_DEFAULT;
    b.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    b.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    b.StructureByteStride = sizeof(UINT);
    HRESULT hr = dev->CreateBuffer(&b, nullptr, &e.result);
    if (SUCCEEDED(hr)) {
        D3D11_UNORDERED_ACCESS_VIEW_DESC u{};
        u.Format = DXGI_FORMAT_UNKNOWN;
        u.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        u.Buffer.NumElements = kSlots * 2;
        hr = dev->CreateUnorderedAccessView(e.result, &u, &e.uav);
    }
    for (int i = 0; SUCCEEDED(hr) && i < kRing; i++) {
        D3D11_BUFFER_DESC s{};
        s.ByteWidth = kSlots * 2 * sizeof(UINT);
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

void Collect(ID3D11DeviceContext* ctx, int eye, EyeState& e) {
    for (int k = 1; k <= kRing; k++) {
        const int i = (e.next + k) % kRing;  // oldest first
        if (!e.pending[i]) continue;
        D3D11_MAPPED_SUBRESOURCE m{};
        if (ctx->Map(e.staging[i], 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &m) != S_OK) continue;
        static UINT v[kSlots * 2];
        memcpy(v, m.pData, sizeof(v));
        ctx->Unmap(e.staging[i], 0);
        e.pending[i] = false;
        float nearest = 0.0f;
        unsigned draws = 0;
        int best = -1;
        for (int s = 0; s < e.counts[i]; s++) {
            if (!v[s * 2 + 1]) continue;
            float z;
            memcpy(&z, &v[s * 2], 4);
            draws++;
            if (z > nearest) {
                nearest = z;
                best = s;
            }
        }
        e.nearest = nearest;
        e.draws = draws;
        e.have = true;
        // Every ~2 s: what reached the middle of the reticle, nearest first (testing)
        if (e.results++ % g_logEvery == 0 && draws) {
            std::string list;
            for (int s = 0; s < e.counts[i] && list.size() < 700; s++) {
                if (!v[s * 2 + 1]) continue;
                float z;
                memcpy(&z, &v[s * 2], 4);
                char item[64];
                sprintf_s(item, " %08X/%u:%.5f(%u)", e.slots[i][s].ps, e.slots[i][s].verts, z, v[s * 2 + 1]);
                list += item;
            }
            LogPrintf("Reticle probe: eye %d, %u of %d draws reach the middle, nearest %.5f by %08X:%s", eye, draws,
                      e.counts[i], nearest, best >= 0 ? e.slots[i][best].ps : 0, list.c_str());
        }
    }
}

}  // namespace

void ReticleProbeSetLogEvery(int results) {
    std::lock_guard<std::mutex> lk(g_lock);
    g_logEvery = results > 0 ? results : 120;
}

void ReticleProbeFrame() {
    std::lock_guard<std::mutex> lk(g_lock);
    for (auto& e : g_eye) {
        e.count = 0;
        e.cleared = false;
        e.ended = false;
    }
}

void ReticleProbeDraw(ID3D11DeviceContext* ctx, int eye, uint32_t ps, unsigned verts,
                      const std::function<void()>& draw) {
    if (eye < 0 || eye > 1) return;
    float x, y, hx, hy;
    if (!ReticlePosAim(eye, x, y, hx, hy)) return;  // nowhere to look yet
    std::lock_guard<std::mutex> lk(g_lock);
    if (g_failed) return;
    EyeState& e = g_eye[eye];
    if (e.ended || e.count >= kSlots) return;
    ID3D11Device* dev = nullptr;
    ctx->GetDevice(&dev);
    const bool ok = dev && (g_ps[0] || BuildShaders(dev)) && PrepareEye(dev, e);
    if (dev) dev->Release();
    if (!ok) return;

    // The box, in this draw's viewport pixels
    D3D11_VIEWPORT vp{};
    UINT nvp = 1;
    ctx->RSGetViewports(&nvp, &vp);
    if (!nvp || vp.Width <= 0 || vp.Height <= 0) return;
    const float cx = vp.TopLeftX + (x + 1.0f) * 0.5f * vp.Width;
    const float cy = vp.TopLeftY + (1.0f - y) * 0.5f * vp.Height;
    const float bx = (std::max)(2.0f, kBox * hx * 0.5f * vp.Width);
    const float by = (std::max)(2.0f, kBox * hy * 0.5f * vp.Height);
    const D3D11_RECT box{ (LONG)floorf(cx - bx), (LONG)floorf(cy - by), (LONG)ceilf(cx + bx), (LONG)ceilf(cy + by) };

    if (!e.cleared) {
        const UINT zero[4] = {};
        ctx->ClearUnorderedAccessViewUint(e.uav, zero);
        e.cleared = true;
    }
    const int slot = e.count++;
    e.frame[slot] = { ps, verts };
    D3D11_MAPPED_SUBRESOURCE m{};
    if (FAILED(ctx->Map(g_cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return;
    const UINT cbData[4] = { (UINT)slot, 0, 0, 0 };
    memcpy(m.pData, cbData, sizeof(cbData));
    ctx->Unmap(g_cb, 0);

    // Save what we change
    ID3D11PixelShader* psSaved = nullptr;
    ID3D11ClassInstance* inst[256];
    UINT ninst = 256;
    ctx->PSGetShader(&psSaved, inst, &ninst);
    ID3D11Buffer* cbSaved = nullptr;
    ctx->PSGetConstantBuffers(13, 1, &cbSaved);
    ID3D11ShaderResourceView* srv0 = nullptr;
    ctx->PSGetShaderResources(0, 1, &srv0);
    ID3D11RenderTargetView* rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
    ID3D11DepthStencilView* dsv = nullptr;
    ctx->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtvs, &dsv);
    ID3D11DepthStencilState* dss = nullptr;
    UINT stencilRef = 0;
    ctx->OMGetDepthStencilState(&dss, &stencilRef);
    ID3D11RasterizerState* rs = nullptr;
    ctx->RSGetState(&rs);
    D3D11_RECT scissors[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
    UINT nScissors = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    ctx->RSGetScissorRects(&nScissors, scissors);

    ctx->PSSetShader(g_ps[srv0 ? 1 : 0], nullptr, 0);
    ctx->PSSetConstantBuffers(13, 1, &g_cb);
    ctx->OMSetRenderTargetsAndUnorderedAccessViews(0, nullptr, nullptr, 0, 1, &e.uav, nullptr);
    ctx->OMSetDepthStencilState(g_noDepth, 0);
    ctx->RSSetState(g_raster);
    ctx->RSSetScissorRects(1, &box);
    draw();

    // Restore
    ctx->RSSetScissorRects(nScissors, nScissors ? scissors : nullptr);
    ctx->RSSetState(rs);
    ctx->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtvs, dsv);
    ctx->OMSetDepthStencilState(dss, stencilRef);
    ctx->PSSetConstantBuffers(13, 1, &cbSaved);
    ctx->PSSetShader(psSaved, inst, ninst);
    for (auto* r : rtvs)
        if (r) r->Release();
    if (dsv) dsv->Release();
    if (dss) dss->Release();
    if (rs) rs->Release();
    if (cbSaved) cbSaved->Release();
    if (srv0) srv0->Release();
    if (psSaved) psSaved->Release();
    for (UINT i = 0; i < ninst; i++)
        if (inst[i]) inst[i]->Release();
}

void ReticleProbeEnd(ID3D11DeviceContext* ctx, int eye) {
    if (eye < 0 || eye > 1) return;
    std::lock_guard<std::mutex> lk(g_lock);
    EyeState& e = g_eye[eye];
    if (g_failed || !e.result || e.ended) return;
    e.ended = true;
    Collect(ctx, eye, e);
    if (!e.cleared) return;  // nothing drawn this frame
    ctx->CopyResource(e.staging[e.next], e.result);
    memcpy(e.slots[e.next], e.frame, sizeof(Slot) * e.count);
    e.counts[e.next] = e.count;
    e.pending[e.next] = true;
    e.next = (e.next + 1) % kRing;
}

bool ReticleProbeLatest(float& nearest, unsigned& draws) {
    std::lock_guard<std::mutex> lk(g_lock);
    if (!g_eye[0].have && !g_eye[1].have) return false;
    nearest = (std::max)(g_eye[0].nearest, g_eye[1].nearest);
    draws = (std::max)(g_eye[0].draws, g_eye[1].draws);
    return true;
}
