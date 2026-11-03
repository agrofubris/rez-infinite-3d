#include "reticle_depth.h"

#include <d3dcompiler.h>
#include <windows.h>

#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

#include "log.h"
#include "reticle_vs.h"

namespace {

std::mutex g_lock;
bool g_failed = false;  // gave up after an error (logged once)
std::string g_probe;  // testing: "x,y,r" = sample a grid over that box of the screen instead
ID3D11Device* g_dev = nullptr;
ID3D11PixelShader* g_ps = nullptr;
ID3D11DepthStencilState* g_noDepth = nullptr;
const int kRing = 4;  // results are read this many measurements later at most

struct EyeState {
    ID3D11Texture2D* copy = nullptr;
    ID3D11ShaderResourceView* srv = nullptr;
    UINT w = 0, h = 0;
    DXGI_FORMAT fmt = DXGI_FORMAT_UNKNOWN;
    ID3D11Buffer* result = nullptr;
    ID3D11UnorderedAccessView* uav = nullptr;
    ID3D11Buffer* staging[kRing] = {};
    bool pending[kRing] = {};
    int next = 0;
    bool have = false;
    float nearest = 0.0f, farthest = 0.0f;
    unsigned samples = 0;
    unsigned measured = 0;
};
EyeState g_eye[2];

void Fail(const char* what, HRESULT hr) {
    g_failed = true;
    LogPrintf("Reticle depth: %s failed (0x%08lX), depth measuring is off", what, (unsigned long)hr);
}

// Typeless storage and readable view for a depth format
bool DepthFormats(DXGI_FORMAT depth, DXGI_FORMAT& typeless, DXGI_FORMAT& view) {
    switch (depth) {
    case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
    case DXGI_FORMAT_R32G8X24_TYPELESS:
        typeless = DXGI_FORMAT_R32G8X24_TYPELESS;
        view = DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
        return true;
    case DXGI_FORMAT_D32_FLOAT:
    case DXGI_FORMAT_R32_TYPELESS:
        typeless = DXGI_FORMAT_R32_TYPELESS;
        view = DXGI_FORMAT_R32_FLOAT;
        return true;
    case DXGI_FORMAT_D24_UNORM_S8_UINT:
    case DXGI_FORMAT_R24G8_TYPELESS:
        typeless = DXGI_FORMAT_R24G8_TYPELESS;
        view = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
        return true;
    case DXGI_FORMAT_D16_UNORM:
    case DXGI_FORMAT_R16_TYPELESS:
        typeless = DXGI_FORMAT_R16_TYPELESS;
        view = DXGI_FORMAT_R16_UNORM;
        return true;
    default:
        return false;
    }
}

// Our pixel shader: its inputs copy the reticle vertex shader's outputs, so the two link.
// Pixels in the middle of the quad (the cross) record the depth under them.
bool BuildShader(ID3D11Device* dev) {
    std::string fields, pos, uv, error;
    if (!ReticleVSFields(fields, pos, uv, error)) {
        Fail(error.c_str(), E_FAIL);
        return false;
    }
    HRESULT hr = S_OK;
    // Under the quad's middle (the cross), or, testing, a grid over the whole screen
    std::string sample;
    float px = 0, py = 0, pr = 0;
    if (!g_probe.empty() && !uv.empty() && sscanf_s(g_probe.c_str(), "%f,%f,%f", &px, &py, &pr) == 3) {
        char box[200];
        sprintf_s(box, "    float d = depthTex.Load(int3(float2(%.1f, %.1f) + (%s * 2.0 - 1.0) * %.1f, 0));\n", px, py,
                  uv.c_str(), pr);
        sample = box;
        LogPrintf("Reticle depth: testing, sampling the box %s instead of the reticle", g_probe.c_str());
    } else
        sample = (uv.empty() ? std::string() : "    float2 c = abs(" + uv + " - 0.5);\n"
                                               "    if (c.x > 0.12 || c.y > 0.12) return;\n") +
                 "    float d = depthTex.Load(int3(i." + pos + ".xy, 0));\n";
    const std::string src = "Texture2D<float> depthTex : register(t0);\n"
                            "RWStructuredBuffer<uint> result : register(u0);\n"
                            "struct In {\n" + fields + "};\n"
                            "void main(In i) {\n" + sample +
                            "    InterlockedMin(result[0], asuint(d));\n"
                            "    InterlockedMin(result[1], asuint(1.0 - d));\n"
                            "    InterlockedAdd(result[2], 1);\n"
                            "}\n";
    ID3DBlob* code = nullptr;
    ID3DBlob* errors = nullptr;
    hr = D3DCompile(src.c_str(), src.size(), "reticle_depth", nullptr, nullptr, "main", "ps_5_0",
                    D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
    if (FAILED(hr)) {
        LogPrintf("Reticle depth: shader source:\n%s", src.c_str());
        if (errors) LogPrintf("Reticle depth: %s", (const char*)errors->GetBufferPointer());
        if (errors) errors->Release();
        Fail("compiling our pixel shader", hr);
        return false;
    }
    if (errors) errors->Release();
    hr = dev->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &g_ps);
    code->Release();
    if (FAILED(hr)) {
        Fail("creating our pixel shader", hr);
        return false;
    }
    D3D11_DEPTH_STENCIL_DESC dd{};
    dd.DepthEnable = FALSE;
    dd.StencilEnable = FALSE;
    dd.DepthFunc = D3D11_COMPARISON_ALWAYS;
    hr = dev->CreateDepthStencilState(&dd, &g_noDepth);
    if (FAILED(hr)) {
        Fail("creating a depth state", hr);
        return false;
    }
    LogPrintf("Reticle depth: pixel shader ready (%s the quad's texture coordinates)", uv.empty() ? "without" : "with");
    return true;
}

bool PrepareEye(ID3D11Device* dev, EyeState& e, const D3D11_TEXTURE2D_DESC& depth) {
    if (e.copy && e.w == depth.Width && e.h == depth.Height && e.fmt == depth.Format) return true;
    if (e.srv) e.srv->Release();
    if (e.copy) e.copy->Release();
    e.srv = nullptr;
    e.copy = nullptr;
    DXGI_FORMAT typeless, view;
    if (!DepthFormats(depth.Format, typeless, view)) {
        char what[64];
        sprintf_s(what, "handling depth format %u", (unsigned)depth.Format);
        Fail(what, E_FAIL);
        return false;
    }
    D3D11_TEXTURE2D_DESC d = depth;
    d.Format = typeless;
    d.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
    d.CPUAccessFlags = 0;
    d.MiscFlags = 0;
    d.Usage = D3D11_USAGE_DEFAULT;
    HRESULT hr = dev->CreateTexture2D(&d, nullptr, &e.copy);
    if (FAILED(hr)) {
        Fail("creating the depth copy", hr);
        return false;
    }
    D3D11_SHADER_RESOURCE_VIEW_DESC sv{};
    sv.Format = view;
    sv.ViewDimension = depth.SampleDesc.Count > 1 ? D3D11_SRV_DIMENSION_TEXTURE2DMS : D3D11_SRV_DIMENSION_TEXTURE2D;
    sv.Texture2D.MipLevels = 1;
    hr = dev->CreateShaderResourceView(e.copy, &sv, &e.srv);
    if (FAILED(hr)) {
        Fail("viewing the depth copy", hr);
        return false;
    }
    if (depth.SampleDesc.Count > 1) {
        Fail("reading a multisampled depth buffer (not supported yet)", E_FAIL);
        return false;
    }
    e.w = depth.Width;
    e.h = depth.Height;
    e.fmt = depth.Format;
    if (!e.result) {
        D3D11_BUFFER_DESC b{};
        b.ByteWidth = 3 * sizeof(UINT);
        b.Usage = D3D11_USAGE_DEFAULT;
        b.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
        b.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        b.StructureByteStride = sizeof(UINT);
        hr = dev->CreateBuffer(&b, nullptr, &e.result);
        if (SUCCEEDED(hr)) {
            D3D11_UNORDERED_ACCESS_VIEW_DESC u{};
            u.Format = DXGI_FORMAT_UNKNOWN;
            u.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
            u.Buffer.NumElements = 3;
            hr = dev->CreateUnorderedAccessView(e.result, &u, &e.uav);
        }
        for (int i = 0; SUCCEEDED(hr) && i < kRing; i++) {
            D3D11_BUFFER_DESC s{};
            s.ByteWidth = 3 * sizeof(UINT);
            s.Usage = D3D11_USAGE_STAGING;
            s.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            hr = dev->CreateBuffer(&s, nullptr, &e.staging[i]);
        }
        if (FAILED(hr)) {
            Fail("creating the result buffers", hr);
            return false;
        }
    }
    LogPrintf("Reticle depth: depth copy %ux%u fmt%u", depth.Width, depth.Height, (unsigned)depth.Format);
    return true;
}

// Picks up results that have come back, without waiting
void Collect(ID3D11DeviceContext* ctx, EyeState& e) {
    for (int k = 1; k <= kRing; k++) {
        const int i = (e.next + k) % kRing;  // oldest first
        if (!e.pending[i]) continue;
        D3D11_MAPPED_SUBRESOURCE m{};
        if (ctx->Map(e.staging[i], 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &m) != S_OK) continue;
        const UINT* v = static_cast<const UINT*>(m.pData);
        const UINT nearBits = v[0], farBits = v[1], count = v[2] + 1;  // the count starts at 0xFFFFFFFF
        ctx->Unmap(e.staging[i], 0);
        e.pending[i] = false;
        float nearest, invFar;
        memcpy(&nearest, &nearBits, 4);
        memcpy(&invFar, &farBits, 4);
        e.samples = count;
        e.nearest = count ? nearest : 0.0f;
        e.farthest = count ? 1.0f - invFar : 0.0f;
        e.have = true;
    }
}

}  // namespace

void ReticleDepthSetProbe(const char* box) { g_probe = box ? box : ""; }

void ReticleDepthMeasure(ID3D11DeviceContext* ctx, int eye, const std::function<void()>& draw) {
    if (eye < 0 || eye > 1) return;
    std::lock_guard<std::mutex> lk(g_lock);
    if (g_failed) return;
    ID3D11DepthStencilView* dsv = nullptr;
    ctx->OMGetRenderTargets(0, nullptr, &dsv);
    if (!dsv) return;  // nothing to read this time
    ID3D11Resource* res = nullptr;
    dsv->GetResource(&res);
    dsv->Release();
    ID3D11Texture2D* depth = nullptr;
    if (!res || FAILED(res->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&depth))) {
        if (res) res->Release();
        return;
    }
    res->Release();
    D3D11_TEXTURE2D_DESC dd{};
    depth->GetDesc(&dd);
    ID3D11Device* dev = nullptr;
    ctx->GetDevice(&dev);
    bool ok = dev && (g_ps || BuildShader(dev)) && PrepareEye(dev, g_eye[eye], dd);
    if (dev) dev->Release();
    if (!ok) {
        depth->Release();
        return;
    }
    EyeState& e = g_eye[eye];
    Collect(ctx, e);
    ctx->CopyResource(e.copy, depth);
    depth->Release();

    // Save what we change
    ID3D11PixelShader* ps = nullptr;
    ID3D11ClassInstance* inst[256];
    UINT ninst = 256;
    ctx->PSGetShader(&ps, inst, &ninst);
    ID3D11ShaderResourceView* srv0 = nullptr;
    ctx->PSGetShaderResources(0, 1, &srv0);
    ID3D11RenderTargetView* rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
    ID3D11DepthStencilView* dsvSaved = nullptr;
    ctx->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtvs, &dsvSaved);
    ID3D11DepthStencilState* dss = nullptr;
    UINT stencilRef = 0;
    ctx->OMGetDepthStencilState(&dss, &stencilRef);

    // The quad again, into our result instead of the picture
    const UINT clear[4] = { 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu };
    ctx->ClearUnorderedAccessViewUint(e.uav, clear);
    ctx->PSSetShader(g_ps, nullptr, 0);
    ctx->PSSetShaderResources(0, 1, &e.srv);
    ctx->OMSetRenderTargetsAndUnorderedAccessViews(0, nullptr, nullptr, 0, 1, &e.uav, nullptr);
    ctx->OMSetDepthStencilState(g_noDepth, 0);
    draw();

    // Restore
    ctx->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtvs, dsvSaved);
    ctx->OMSetDepthStencilState(dss, stencilRef);
    ctx->PSSetShaderResources(0, 1, &srv0);
    ctx->PSSetShader(ps, inst, ninst);
    for (auto* r : rtvs)
        if (r) r->Release();
    if (dsvSaved) dsvSaved->Release();
    if (dss) dss->Release();
    if (srv0) srv0->Release();
    if (ps) ps->Release();
    for (UINT i = 0; i < ninst; i++)
        if (inst[i]) inst[i]->Release();

    ctx->CopyResource(e.staging[e.next], e.result);
    e.pending[e.next] = true;
    e.next = (e.next + 1) % kRing;
    if (++e.measured % 120 == 1 && e.have)
        LogPrintf("Reticle depth: eye %d nearest %.7f farthest %.7f samples %u", eye, e.nearest, e.farthest, e.samples);
}

bool ReticleDepthLatest(int eye, float& nearest, float& farthest, unsigned& samples) {
    if (eye < 0 || eye > 1) return false;
    std::lock_guard<std::mutex> lk(g_lock);
    const EyeState& e = g_eye[eye];
    if (!e.have) return false;
    nearest = e.nearest;
    farthest = e.farthest;
    samples = e.samples;
    return true;
}
