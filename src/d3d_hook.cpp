#include "d3d_hook.h"

#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "MinHook.h"
#include "config.h"
#include "log.h"
#include "reticle_depth.h"
#include "reticle_pos.h"
#include "reticle_probe.h"
#include "reticle_scale.h"
#include "reticle_vs.h"

namespace {

// vtable slots (d3d11.h declaration order, after IUnknown / ID3D11DeviceChild). Each
// context object carries its own copy of the table, so the hooks patch the d3d11 code
// the tables point to: that reaches every context, however early it was created.
enum DeviceSlot { kCreateVertexShader = 12, kCreatePixelShader = 15 };
enum ContextSlot {
    kPSSetShader = 9,
    kVSSetShader = 11,
    kDrawIndexed = 12,
    kDraw = 13,
    kDrawIndexedInstanced = 20,
    kDrawInstanced = 21,
};

using CreateVSFn = HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*, const void*, SIZE_T, ID3D11ClassLinkage*,
                                               ID3D11VertexShader**);
using CreatePSFn = HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*, const void*, SIZE_T, ID3D11ClassLinkage*,
                                               ID3D11PixelShader**);
using SetVSFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11VertexShader*, ID3D11ClassInstance* const*, UINT);
using SetPSFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11PixelShader*, ID3D11ClassInstance* const*, UINT);
using DrawIndexedFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, INT);
using DrawFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT);
using DrawIndexedInstancedFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, UINT, INT, UINT);
using DrawInstancedFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, UINT, UINT);

StereoConfig g_cfg;  // copy of the presenter's settings
ID3D11Device* g_gameDev = nullptr;
ID3D11Device* g_ownDev = nullptr;  // the presenter's device: never recorded or skipped
bool g_installed = false;
std::mutex g_lock;
std::unordered_map<void*, uint32_t> g_shaderHash;  // shader object -> hash of its bytecode
struct Bound {
    void* vs = nullptr;
    void* ps = nullptr;
};
std::unordered_map<ID3D11DeviceContext*, Bound> g_bound;
std::unordered_map<ID3D11DeviceContext*, ID3D11Device*> g_seenCtx;  // contexts that drew, with their device

// This frame's draws, grouped by shader pair, in first-draw order
struct DrawGroup {
    uint32_t vs, ps;
    unsigned draws, verts;
    void* dsv;  // the depth buffer of its first draw
};
std::vector<DrawGroup> g_frame;
unsigned g_drawsGame = 0, g_drawsOther = 0;
// Distinct (pixel shader, render target, depth buffer) of this frame's draws, in order (logged frames)
struct Target {
    uint32_t ps;
    void* rt;
    void* ds;
    unsigned draws;
};
std::vector<Target> g_targets;
std::vector<uint32_t> g_huntList;  // pixel shaders to skip one by one
LARGE_INTEGER g_t0{}, g_freq{};
uint32_t g_skipPS = 0;
unsigned g_frameNo = 0;
// One pixel shader's draws in order (HookDetailPS / HookHuntDrawsOf)
unsigned g_ordinal = 0, g_lastOrdinals = 0;
unsigned g_reticleDraws = 0;  // this frame's draws that match the reticle's signature
// The eye each texture the game submits belongs to; the game draws each eye straight into it
std::unordered_map<void*, int> g_eyeOfTex;
thread_local float t_shift = 0.0f;     // set by OnDraw: shift this draw's viewport (pixels)...
thread_local float t_shiftNdc = 0.0f;  // ...or by this share of half its width (NDC units)
thread_local int t_measureEye = -1;    // measure the depth under this draw (the reticle's octagon) for this eye
thread_local bool t_scale = false;     // draw this one in the reticle's shape (ReticleScale, ReticleFlat)...
thread_local int t_shapeEye = -1;      // ...for this eye (-1 = not an eye image: scaled, not flattened)
thread_local int t_probeEye = -1;      // look for what this draw puts under the reticle, in this eye
thread_local uint32_t t_probePS = 0;
thread_local int t_posEye = -1;        // measure where this draw (the reticle's octagon) is shown in this eye
std::atomic<float> g_sepNdc{ 0.0f }, g_convergence{ 0.0f };
std::atomic<float> g_targetInvZ{ 0.0f };  // where the reticle goes, 1 / meters (0 = not decided yet)
float g_smoothInvZ = 0.0f;
LARGE_INTEGER g_lastTarget{};
unsigned g_shiftLogs = 0;

int EyeOfRenderTarget(ID3D11DeviceContext* ctx) {
    ID3D11RenderTargetView* rtv = nullptr;
    ctx->OMGetRenderTargets(1, &rtv, nullptr);
    if (!rtv) return -1;
    ID3D11Resource* res = nullptr;
    rtv->GetResource(&res);
    rtv->Release();
    int eye = -1;
    if (res) {
        auto it = g_eyeOfTex.find(res);
        if (it != g_eyeOfTex.end()) eye = it->second;
        res->Release();
    }
    return eye;
}
// HookHuntTexOf: the textures (slot 0) that pixel shader sampled this frame, in first-use order
struct TexSeen {
    void* tex;
    std::string desc;
};
std::vector<TexSeen> g_texFrame, g_texHunt;
void* g_skipTex = nullptr;
int g_skipOrdinal = -1;
std::vector<std::string> g_detail;

uint32_t Fnv1a(const void* data, SIZE_T size) {
    const unsigned char* p = static_cast<const unsigned char*>(data);
    uint32_t h = 2166136261u;
    for (SIZE_T i = 0; i < size; i++) h = (h ^ p[i]) * 16777619u;
    return h;
}

uint32_t HashOf(void* shader) {
    if (!shader) return 0;
    auto it = g_shaderHash.find(shader);
    return it != g_shaderHash.end() ? it->second : 1;  // 1 = created before the hook
}

ID3D11Device* DeviceOf(ID3D11DeviceChild* obj) {
    ID3D11Device* d = nullptr;
    obj->GetDevice(&d);
    if (d) d->Release();
    return d;
}

// One hook per distinct target: several tables may share an implementation
template <typename Fn>
struct Hooked {
    std::vector<std::pair<void*, Fn>> originals;  // target -> its trampoline
    Fn Original(void* target) const {
        for (auto& p : originals)
            if (p.first == target) return p.second;
        return nullptr;
    }
};

Hooked<CreateVSFn> g_createVS;
Hooked<CreatePSFn> g_createPS;
Hooked<SetVSFn> g_setVS;
Hooked<SetPSFn> g_setPS;
Hooked<DrawIndexedFn> g_drawIndexed;
Hooked<DrawFn> g_draw;
Hooked<DrawIndexedInstancedFn> g_drawIndexedInstanced;
Hooked<DrawInstancedFn> g_drawInstanced;

// Each detour must call the trampoline of the target its caller's table points to
template <typename Fn>
Fn OriginalFor(const Hooked<Fn>& h, void* obj, int slot) {
    void* target = (*reinterpret_cast<void***>(obj))[slot];
    Fn fn = h.Original(target);
    return fn ? fn : (h.originals.empty() ? nullptr : h.originals.front().second);
}

HRESULT STDMETHODCALLTYPE HookCreateVS(ID3D11Device* dev, const void* code, SIZE_T size, ID3D11ClassLinkage* link,
                                       ID3D11VertexShader** out) {
    HRESULT hr = OriginalFor(g_createVS, dev, kCreateVertexShader)(dev, code, size, link, out);
    if (SUCCEEDED(hr) && out && *out && code && dev != g_ownDev) {
        const uint32_t h = Fnv1a(code, size);
        std::lock_guard<std::mutex> lk(g_lock);
        g_shaderHash[*out] = h;
        if (h == g_cfg.reticleVS) ReticleVSSetCode(code, size);
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE HookCreatePS(ID3D11Device* dev, const void* code, SIZE_T size, ID3D11ClassLinkage* link,
                                       ID3D11PixelShader** out) {
    HRESULT hr = OriginalFor(g_createPS, dev, kCreatePixelShader)(dev, code, size, link, out);
    if (SUCCEEDED(hr) && out && *out && code && dev != g_ownDev) {
        std::lock_guard<std::mutex> lk(g_lock);
        g_shaderHash[*out] = Fnv1a(code, size);
    }
    return hr;
}

void STDMETHODCALLTYPE HookSetVS(ID3D11DeviceContext* ctx, ID3D11VertexShader* s, ID3D11ClassInstance* const* ci,
                                 UINT n) {
    {
        std::lock_guard<std::mutex> lk(g_lock);
        g_bound[ctx].vs = s;
    }
    OriginalFor(g_setVS, ctx, kVSSetShader)(ctx, s, ci, n);
}

void STDMETHODCALLTYPE HookSetPS(ID3D11DeviceContext* ctx, ID3D11PixelShader* s, ID3D11ClassInstance* const* ci,
                                 UINT n) {
    {
        std::lock_guard<std::mutex> lk(g_lock);
        g_bound[ctx].ps = s;
    }
    OriginalFor(g_setPS, ctx, kPSSetShader)(ctx, s, ci, n);
}

// What a draw of the detailed pixel shader samples and where it draws
std::string DrawDetail(ID3D11DeviceContext* ctx, unsigned ord, const char* kind, UINT count, UINT inst) {
    char b[320];
    std::string tex = "-", rt = "-";
    ID3D11ShaderResourceView* srv = nullptr;
    ctx->PSGetShaderResources(0, 1, &srv);
    if (srv) {
        ID3D11Resource* res = nullptr;
        srv->GetResource(&res);
        ID3D11Texture2D* t = nullptr;
        if (res && SUCCEEDED(res->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&t))) {
            D3D11_TEXTURE2D_DESC d{};
            t->GetDesc(&d);
            sprintf_s(b, "%p %ux%u fmt%u", (void*)t, d.Width, d.Height, (unsigned)d.Format);
            tex = b;
            t->Release();
        }
        if (res) res->Release();
        srv->Release();
    }
    ID3D11RenderTargetView* rtv = nullptr;
    ctx->OMGetRenderTargets(1, &rtv, nullptr);
    if (rtv) {
        ID3D11Resource* res = nullptr;
        rtv->GetResource(&res);
        ID3D11Texture2D* t = nullptr;
        if (res && SUCCEEDED(res->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&t))) {
            D3D11_TEXTURE2D_DESC d{};
            t->GetDesc(&d);
            sprintf_s(b, "%p %ux%u", (void*)t, d.Width, d.Height);
            rt = b;
            t->Release();
        }
        if (res) res->Release();
        rtv->Release();
    }
    UINT nvp = 1;
    D3D11_VIEWPORT vp{};
    ctx->RSGetViewports(&nvp, &vp);
    sprintf_s(b, " [%u %s %u x%u tex %s rt %s vp %.0f,%.0f %.0fx%.0f]", ord, kind, count, inst, tex.c_str(), rt.c_str(),
              vp.TopLeftX, vp.TopLeftY, vp.Width, vp.Height);
    return b;
}

// Whether a comma-separated list of hex hashes ("56E63E45,...") holds this one
bool ListHas(const std::string& list, uint32_t hash) {
    char hex[16];
    sprintf_s(hex, "%08X", hash);
    return ("," + list + ",").find("," + std::string(hex) + ",") != std::string::npos;
}

// The reticle: its pixel shader, vertex count and texture (size and format) in slot 0;
// *format = the texture's format (77 = the octagon, 87 = the centre cross)
bool IsReticleDraw(ID3D11DeviceContext* ctx, uint32_t ps, UINT count, int* format = nullptr) {
    if (!g_cfg.reticlePS || ps != g_cfg.reticlePS || (int)count != g_cfg.reticleVerts) return false;
    ID3D11ShaderResourceView* srv = nullptr;
    ctx->PSGetShaderResources(0, 1, &srv);
    if (!srv) return false;
    bool match = false;
    ID3D11Resource* res = nullptr;
    srv->GetResource(&res);
    ID3D11Texture2D* t = nullptr;
    if (res && SUCCEEDED(res->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&t))) {
        D3D11_TEXTURE2D_DESC d{};
        t->GetDesc(&d);
        if ((int)d.Width == g_cfg.reticleTexW && (int)d.Height == g_cfg.reticleTexH) {
            char fmt[16];
            sprintf_s(fmt, "%u", (unsigned)d.Format);
            const std::string list = "," + g_cfg.reticleTexFormats + ",";
            match = list.find("," + std::string(fmt) + ",") != std::string::npos;
            if (format) *format = (int)d.Format;
        }
        t->Release();
    }
    if (res) res->Release();
    srv->Release();
    return match;
}

// Records a game draw; false = skip it (hunting)
bool OnDraw(ID3D11DeviceContext* ctx, UINT verts, const char* kind = "", UINT count = 0, UINT inst = 1) {
    t_shift = 0.0f;
    t_shiftNdc = 0.0f;
    t_measureEye = -1;
    t_scale = false;
    t_shapeEye = -1;
    t_probeEye = -1;
    t_posEye = -1;
    ID3D11Device* dev = DeviceOf(ctx);
    if (dev == g_ownDev) return true;
    std::lock_guard<std::mutex> lk(g_lock);
    if (!g_seenCtx.count(ctx) && g_seenCtx.size() < 32) {
        g_seenCtx[ctx] = dev;
        LogPrintf("Hook: drawing context %p (device %p, %s)%s", (void*)ctx, (void*)dev,
                  ctx->GetType() == D3D11_DEVICE_CONTEXT_IMMEDIATE ? "immediate" : "deferred",
                  dev == g_gameDev ? ", the device of the eye images" : "");
    }
    (dev == g_gameDev ? g_drawsGame : g_drawsOther)++;
    const Bound& bd = g_bound[ctx];
    const uint32_t vs = HashOf(bd.vs), ps = HashOf(bd.ps);
    bool found = false;
    for (auto& g : g_frame) {
        if (g.vs == vs && g.ps == ps) {
            g.draws++;
            g.verts += verts;
            found = true;
            break;
        }
    }
    {
        void* rtRes = nullptr;
        void* dsRes = nullptr;
        ID3D11RenderTargetView* rtv = nullptr;
        ID3D11DepthStencilView* dsv = nullptr;
        ctx->OMGetRenderTargets(1, &rtv, &dsv);
        if (rtv) {
            ID3D11Resource* r = nullptr;
            rtv->GetResource(&r);
            rtRes = r;
            if (r) r->Release();
            rtv->Release();
        }
        if (dsv) {
            ID3D11Resource* r = nullptr;
            dsv->GetResource(&r);
            dsRes = r;
            if (r) r->Release();
            dsv->Release();
        }
        bool seenTarget = false;
        for (auto& t : g_targets) {
            if (t.ps == ps && t.rt == rtRes && t.ds == dsRes) {
                t.draws++;
                seenTarget = true;
                break;
            }
        }
        if (!seenTarget && g_targets.size() < 64) g_targets.push_back({ ps, rtRes, dsRes, 1 });
    }
    if (!found && g_frame.size() < 256) {
        void* dsvRes = nullptr;
        ID3D11DepthStencilView* dsv = nullptr;
        ctx->OMGetRenderTargets(0, nullptr, &dsv);
        if (dsv) {
            ID3D11Resource* r = nullptr;
            dsv->GetResource(&r);
            dsvRes = r;
            if (r) r->Release();
            dsv->Release();
        }
        g_frame.push_back({ vs, ps, 1, verts, dsvRes });
    }
    if (g_cfg.hookHuntTexOf && ps == g_cfg.hookHuntTexOf) {
        ID3D11ShaderResourceView* srv = nullptr;
        ctx->PSGetShaderResources(0, 1, &srv);
        void* tex = nullptr;
        char desc[64] = "-";
        if (srv) {
            ID3D11Resource* res = nullptr;
            srv->GetResource(&res);
            ID3D11Texture2D* t = nullptr;
            if (res && SUCCEEDED(res->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&t))) {
                D3D11_TEXTURE2D_DESC d{};
                t->GetDesc(&d);
                sprintf_s(desc, "%ux%u fmt%u", d.Width, d.Height, (unsigned)d.Format);
                tex = t;
                t->Release();
            }
            if (res) res->Release();
            srv->Release();
        }
        bool seen = false;
        for (auto& x : g_texFrame) seen |= x.tex == tex;
        if (!seen && g_texFrame.size() < 32) g_texFrame.push_back({ tex, desc });
        if (g_skipTex && tex == g_skipTex) return false;
    }
    int reticleFormat = 0;
    if (IsReticleDraw(ctx, ps, count, &reticleFormat)) {
        g_reticleDraws++;
        if (g_cfg.hookSkipReticle) return false;
        t_scale = (g_cfg.reticleScale > 0.0f && g_cfg.reticleScale != 1.0f) || g_cfg.reticleFlat;
        if (t_scale) t_shapeEye = EyeOfRenderTarget(ctx);
        if (g_cfg.reticleAuto && reticleFormat == 77) t_measureEye = EyeOfRenderTarget(ctx);
        if ((g_cfg.reticleKeepInView || g_cfg.reticleAuto) && reticleFormat == 77) t_posEye = EyeOfRenderTarget(ctx);
        if (g_cfg.reticleShiftPx != 0.0f || g_cfg.reticleDepth > 0.0f || g_cfg.reticleAuto) {
            const int eye = EyeOfRenderTarget(ctx);
            const float sign = eye == 0 ? 1.0f : -1.0f;  // right in the left eye = nearer
            if (eye >= 0 && g_cfg.reticleShiftPx != 0.0f) {
                t_shift = sign * g_cfg.reticleShiftPx;
            } else if (eye >= 0 && g_targetInvZ.load() > 0.0f) {
                // Each eye's offset of a point z meters away is -+sep * (1 - C / z): moving the
                // reticle from the game's distance D to the target z takes sep * C * (1/z - 1/D)
                const float d = g_cfg.reticleGameDistance > 0.0f ? g_cfg.reticleGameDistance : 1e9f;
                t_shiftNdc = sign * g_sepNdc.load() * g_convergence.load() * (g_targetInvZ.load() - 1.0f / d);
            }
            if (g_shiftLogs < 6) {
                g_shiftLogs++;
                LogPrintf("Hook: reticle drawn into eye %d, shift %+.1f px / %+.4f NDC", eye, t_shift, t_shiftNdc);
                // The depth buffer bound at the reticle's draw (for a depth that follows the target)
                ID3D11DepthStencilView* dsv = nullptr;
                ctx->OMGetRenderTargets(0, nullptr, &dsv);
                if (dsv) {
                    ID3D11Resource* res = nullptr;
                    dsv->GetResource(&res);
                    ID3D11Texture2D* t = nullptr;
                    if (res && SUCCEEDED(res->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&t))) {
                        D3D11_TEXTURE2D_DESC d{};
                        t->GetDesc(&d);
                        LogPrintf("Hook: reticle's depth buffer %p %ux%u fmt%u samples %u bind 0x%X", (void*)t, d.Width,
                                  d.Height, (unsigned)d.Format, d.SampleDesc.Count, d.BindFlags);
                        t->Release();
                    }
                    if (res) res->Release();
                    dsv->Release();
                } else {
                    LogPrintf("Hook: no depth buffer bound at the reticle's draw");
                }
            }
        }
    } else if (g_cfg.reticleAuto && g_cfg.reticleDepthSource != "buffer" && vs == g_cfg.reticleVS &&
               !ListHas(g_cfg.reticleDepthIgnorePS, ps)) {
        // Anything else drawn into an eye (but glow sprites): what does it put under the middle of the reticle?
        t_probeEye = EyeOfRenderTarget(ctx);
        t_probePS = ps;
    }
    const uint32_t focus = g_cfg.hookHuntDrawsOf ? g_cfg.hookHuntDrawsOf : g_cfg.hookDetailPS;
    if (focus && ps == focus) {
        const unsigned ord = g_ordinal++;
        if (g_cfg.hookDetailPS == ps && g_detail.size() < 128) g_detail.push_back(DrawDetail(ctx, ord, kind, count, inst));
        if (g_skipOrdinal >= 0 && (int)ord == g_skipOrdinal) return false;
    }
    return !(g_skipPS && ps == g_skipPS);
}

// How a reticle quad drawn into this eye is shown: ReticleScale, and ReticleFlat (which needs
// the eye's frustum: its eye shift and the lens shift)
ReticleShape ShapeFor(int eye) {
    ReticleShape s{};
    ReticlePosProjection(s.tanH, s.tanV, s.shiftY);
    s.shiftX = eye == 0 ? g_sepNdc.load() * s.tanH : -g_sepNdc.load() * s.tanH;
    s.scale = g_cfg.reticleScale > 0.0f ? g_cfg.reticleScale : 1.0f;
    s.flat = g_cfg.reticleFlat && eye >= 0 ? 1.0f : 0.0f;
    return s;
}

// Runs a draw with the viewport moved sideways by t_shift pixels, then puts it back; a
// reticle quad is drawn in its shape
template <typename F>
void DrawShifted(ID3D11DeviceContext* ctx, F draw) {
    if (t_scale) {
        t_scale = false;
        ReticleScaleDraw(ctx, ShapeFor(t_shapeEye), [&] { DrawShifted(ctx, draw); });
        return;
    }
    if (t_shift == 0.0f && t_shiftNdc == 0.0f) {
        draw();
        return;
    }
    D3D11_VIEWPORT vps[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
    UINT n = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    ctx->RSGetViewports(&n, vps);
    if (n == 0) {
        draw();
        return;
    }
    D3D11_VIEWPORT moved[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
    for (UINT i = 0; i < n; i++) {
        moved[i] = vps[i];
        moved[i].TopLeftX += t_shift + t_shiftNdc * vps[i].Width * 0.5f;
    }
    ctx->RSSetViewports(n, moved);
    draw();
    ctx->RSSetViewports(n, vps);
}

void STDMETHODCALLTYPE HookDrawIndexed(ID3D11DeviceContext* ctx, UINT count, UINT start, INT base) {
    if (!OnDraw(ctx, count, "idx", count)) return;
    if (t_probeEye >= 0)
        ReticleProbeDraw(ctx, t_probeEye, t_probePS, count,
                         [&] { OriginalFor(g_drawIndexed, ctx, kDrawIndexed)(ctx, count, start, base); });
    DrawShifted(ctx, [&] { OriginalFor(g_drawIndexed, ctx, kDrawIndexed)(ctx, count, start, base); });
}
void STDMETHODCALLTYPE HookDraw(ID3D11DeviceContext* ctx, UINT count, UINT start) {
    if (!OnDraw(ctx, count, "draw", count)) return;
    if (t_probeEye >= 0)
        ReticleProbeDraw(ctx, t_probeEye, t_probePS, count, [&] { OriginalFor(g_draw, ctx, kDraw)(ctx, count, start); });
    if (t_measureEye >= 0) {
        ReticleProbeEnd(ctx, t_measureEye);
        ReticleDepthMeasure(ctx, t_measureEye, [&] { OriginalFor(g_draw, ctx, kDraw)(ctx, count, start); });
    }
    if (t_posEye >= 0)
        ReticlePosMeasure(ctx, t_posEye, t_shiftNdc, ShapeFor(t_posEye),
                          [&] { OriginalFor(g_draw, ctx, kDraw)(ctx, count, start); });
    DrawShifted(ctx, [&] { OriginalFor(g_draw, ctx, kDraw)(ctx, count, start); });
}
void STDMETHODCALLTYPE HookDrawIndexedInstanced(ID3D11DeviceContext* ctx, UINT count, UINT inst, UINT start, INT base,
                                                UINT startInst) {
    if (!OnDraw(ctx, count * inst, "idxinst", count, inst)) return;
    auto draw = [&] { OriginalFor(g_drawIndexedInstanced, ctx, kDrawIndexedInstanced)(ctx, count, inst, start, base, startInst); };
    if (t_probeEye >= 0) ReticleProbeDraw(ctx, t_probeEye, t_probePS, count * inst, draw);
    draw();
}
void STDMETHODCALLTYPE HookDrawInstanced(ID3D11DeviceContext* ctx, UINT count, UINT inst, UINT start, UINT startInst) {
    if (!OnDraw(ctx, count * inst, "inst", count, inst)) return;
    auto draw = [&] { OriginalFor(g_drawInstanced, ctx, kDrawInstanced)(ctx, count, inst, start, startInst); };
    if (t_probeEye >= 0) ReticleProbeDraw(ctx, t_probeEye, t_probePS, count * inst, draw);
    draw();
}

template <typename Fn>
void HookSlot(Hooked<Fn>& h, void* obj, int slot, void* detour, const char* name) {
    void* target = (*reinterpret_cast<void***>(obj))[slot];
    if (h.Original(target)) return;
    Fn original = nullptr;
    MH_STATUS st = MH_CreateHook(target, detour, reinterpret_cast<void**>(&original));
    if (st == MH_OK) st = MH_EnableHook(target);
    if (st != MH_OK) {
        LogPrintf("Hook: %s at %p failed (%s)", name, target, MH_StatusToString(st));
        return;
    }
    h.originals.push_back({ target, original });
    LogPrintf("Hook: %s at %p", name, target);
}

void HookContext(ID3D11DeviceContext* ctx) {
    HookSlot(g_setVS, ctx, kVSSetShader, (void*)HookSetVS, "VSSetShader");
    HookSlot(g_setPS, ctx, kPSSetShader, (void*)HookSetPS, "PSSetShader");
    HookSlot(g_drawIndexed, ctx, kDrawIndexed, (void*)HookDrawIndexed, "DrawIndexed");
    HookSlot(g_draw, ctx, kDraw, (void*)HookDraw, "Draw");
    HookSlot(g_drawIndexedInstanced, ctx, kDrawIndexedInstanced, (void*)HookDrawIndexedInstanced,
             "DrawIndexedInstanced");
    HookSlot(g_drawInstanced, ctx, kDrawInstanced, (void*)HookDrawInstanced, "DrawInstanced");
}

// Hooks the code behind this device's and context's tables (and a deferred context's)
void HookDevice(ID3D11Device* dev, ID3D11DeviceContext* ctx) {
    HookSlot(g_createVS, dev, kCreateVertexShader, (void*)HookCreateVS, "CreateVertexShader");
    HookSlot(g_createPS, dev, kCreatePixelShader, (void*)HookCreatePS, "CreatePixelShader");
    HookContext(ctx);
    ID3D11DeviceContext* deferred = nullptr;
    if (SUCCEEDED(dev->CreateDeferredContext(0, &deferred)) && deferred) {
        HookContext(deferred);
        deferred->Release();
    }
}

bool g_minhook = false;

bool StartMinHook(const StereoConfig& cfg) {
    if (g_minhook) return true;
    g_cfg = cfg;
    g_skipPS = cfg.hookSkipPS;
    QueryPerformanceFrequency(&g_freq);
    QueryPerformanceCounter(&g_t0);
    MH_STATUS st = MH_Initialize();
    if (st != MH_OK && st != MH_ERROR_ALREADY_INITIALIZED) {
        LogPrintf("Hook: MinHook init failed (%s)", MH_StatusToString(st));
        return false;
    }
    g_minhook = true;
    return true;
}

}  // namespace

void D3DHookSetOwnDevice(ID3D11Device* dev) { g_ownDev = dev; }

// The hooks run for the reticle settings and the test modes; never in Area X (other shaders)
bool HooksWanted(const StereoConfig& cfg) {
    if (cfg.areaX) return false;
    return cfg.hookMode > 0 || cfg.reticleAuto || cfg.reticleDepth > 0.0f || cfg.reticleShiftPx != 0.0f ||
           (cfg.reticleScale > 0.0f && cfg.reticleScale != 1.0f) || cfg.reticleKeepInView ||
           cfg.reticleFlat;
}

void D3DHookSetStereo(float separationNdc, float convergence) {
    g_sepNdc = separationNdc;
    g_convergence = convergence;
}

void D3DHookNoteEye(int eye, ID3D11Texture2D* tex) {
    if (!g_installed || !tex) return;
    std::lock_guard<std::mutex> lk(g_lock);
    auto it = g_eyeOfTex.find(tex);
    if (it == g_eyeOfTex.end() || it->second != eye) {
        g_eyeOfTex[tex] = eye;
        LogPrintf("Hook: eye %d texture %p", eye, (void*)tex);
    }
}

// At VR init, before the game makes its shaders: d3d11's code is shared by every device,
// so a scratch device's tables lead to the same functions (unless the game's device uses
// other variants; the first eye image then hooks those too)
void InstallD3DHooksEarly(const StereoConfig& cfg) {
    if (!HooksWanted(cfg) || g_minhook || !StartMinHook(cfg)) return;
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &dev,
                                 nullptr, &ctx))) {
        LogPrintf("Hook: no scratch device for the early hooks");
        return;
    }
    HookDevice(dev, ctx);
    ctx->Release();
    dev->Release();
    LogPrintf("Hook: early hooks from a scratch device (mode %d)", cfg.hookMode);
}

float NearLimit();

void InstallD3DHooks(ID3D11Device* dev, ID3D11DeviceContext* ctx, const StereoConfig& cfg) {
    if (g_installed || !dev || !ctx || !HooksWanted(cfg) || !StartMinHook(cfg)) return;
    g_installed = true;
    g_gameDev = dev;
    ReticleDepthSetProbe(g_cfg.reticleDepthProbe.c_str());
    ReticleProbeSetLogEvery(g_cfg.reticleProbeLogEvery);
    ReticleProbeSetNearLimit(NearLimit());
    // Over the octagon at the game's own size, the area Rez locks onto, however large we draw it
    ReticleProbeSetBox(g_cfg.reticleDepthBox / (g_cfg.reticleScale > 0.0f ? g_cfg.reticleScale : 1.0f));
    HookDevice(dev, ctx);
    LogPrintf("Hook: D3D11 draw hooks on the eye images' device %p (mode %d, skip reticle %d, always skipping PS %08X)",
              (void*)dev, g_cfg.hookMode, g_cfg.hookSkipReticle, g_cfg.hookSkipPS);
}


const int kRecent = 7;  // frames the reticle depth takes the median of

// The depth value of ReticleIgnoreNearer: anything nearer (the avatar, the floor just below it, the
// shots leaving it) is not something to aim at, so auto ignores it (reversed: larger = nearer)
float NearLimit() {
    return g_cfg.reticleIgnoreNearer > 0.0f && g_cfg.reticleDepthScale > 0.0f ? g_cfg.reticleDepthScale / g_cfg.reticleIgnoreNearer
                                                                           : 0.0f;
}

// Where the reticle goes this frame, in 1 / meters
void UpdateReticleTarget() {
    const float gameInv = g_cfg.reticleGameDistance > 0.0f ? 1.0f / g_cfg.reticleGameDistance : 0.0f;
    const float fixedInv = g_cfg.reticleDepth > 0.0f ? 1.0f / g_cfg.reticleDepth : gameInv;
    if (!g_cfg.reticleAuto) {
        g_targetInvZ = g_cfg.reticleDepth > 0.0f ? fixedInv : 0.0f;
        return;
    }
    // Reversed depth: the larger value is the nearer. Both eyes see the reticle's centre.
    float bufferDepth = 0.0f;
    bool any = false;
    for (int eye = 0; eye < 2; eye++) {
        float lo, hi;
        unsigned samples;
        if (ReticleDepthLatest(eye, lo, hi, samples)) {
            any = true;
            if (samples && hi > bufferDepth) bufferDepth = hi;
        }
    }
    // What is drawn there, depth-writing or not (ReticleDepthSource=draws, the default): the
    // nearer of that and the depth buffer
    float drawnDepth = 0.0f;
    unsigned drawnBy = 0;
    const bool probed = g_cfg.reticleDepthSource != "buffer" && ReticleProbeLatest(drawnDepth, drawnBy);
    if (!any && !probed) return;
    const float nearLimit = NearLimit();
    if (nearLimit > 0.0f && bufferDepth > nearLimit) bufferDepth = 0.0f;  // too near: can't see past it
    const float measured = probed && drawnDepth > bufferDepth ? drawnDepth : bufferDepth;
    // The median of the last few frames (about 12 ms apart): shots and sparks crossing the
    // middle of the reticle for a frame or two don't move it; a target that stays does
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    static float recent[kRecent] = {};
    static int recentNext = 0, recentCount = 0;
    static LARGE_INTEGER lastSample{};
    if (!lastSample.QuadPart || now.QuadPart - lastSample.QuadPart >= g_freq.QuadPart * 12 / 1000) {
        lastSample = now;
        recent[recentNext] = measured;
        recentNext = (recentNext + 1) % kRecent;
        if (recentCount < kRecent) recentCount++;
    }
    float sorted[kRecent];
    memcpy(sorted, recent, sizeof(sorted));
    std::sort(sorted, sorted + recentCount);
    const float nearestDepth = recentCount ? sorted[recentCount / 2] : measured;
    const float inv = nearestDepth > 0.0f && g_cfg.reticleDepthScale > 0.0f ? nearestDepth / g_cfg.reticleDepthScale : fixedInv;
    // Ease toward it (about 0.12 s), in 1/z so near and far changes feel alike
    const float dt = g_lastTarget.QuadPart ? (float)((double)(now.QuadPart - g_lastTarget.QuadPart) / (double)g_freq.QuadPart) : 1.0f;
    g_lastTarget = now;
    const float k = 1.0f - expf(-(dt < 0.5f ? dt : 0.5f) / 0.12f);
    g_smoothInvZ = g_smoothInvZ > 0.0f ? g_smoothInvZ + (inv - g_smoothInvZ) * k : inv;
    g_targetInvZ = g_smoothInvZ;
    static LARGE_INTEGER lastLog{};
    if (!lastLog.QuadPart || now.QuadPart - lastLog.QuadPart > 2 * g_freq.QuadPart) {
        lastLog = now;
        LogPrintf("Reticle depth: target %.1f m (nearest under the reticle: drawn %.5f by %u draws, depth buffer %.5f%s)",
                  1.0f / g_smoothInvZ, drawnDepth, drawnBy, bufferDepth, nearestDepth > 0.0f ? "" : ", nothing: fallback");
    }
}

void D3DHookFrame() {
    if (!g_installed) return;
    UpdateReticleTarget();
    std::lock_guard<std::mutex> lk(g_lock);
    // Rez submits the same eye images many times between frames: only intervals
    // with draws are frames
    if (g_frame.empty()) return;
    ReticleProbeFrame();
    g_frameNo++;
    if (g_cfg.hookMode > 0 && g_cfg.hookLogEvery > 0 && g_frameNo % (unsigned)g_cfg.hookLogEvery == 0) {
        std::string s;
        for (auto& g : g_frame) {
            char b[96];
            sprintf_s(b, " %08X/%08X:%ux%u(ds %p)", g.vs, g.ps, g.draws, g.verts, g.dsv);
            s += b;
        }
        LogPrintf("Hook: frame %u, draws %u on the eye images' device / %u on others, reticle draws %u, %zu shader "
                  "pairs (vs/ps:draws x verts):%s",
                  g_frameNo, g_drawsGame, g_drawsOther, g_reticleDraws, g_frame.size(), s.c_str());
        std::string t;
        for (auto& x : g_targets) {
            char b[96];
            sprintf_s(b, " %08X rt %p ds %p x%u;", x.ps, x.rt, x.ds, x.draws);
            t += b;
        }
        LogPrintf("Hook: frame %u targets (ps rt ds draws):%s", g_frameNo, t.c_str());
        if (!g_detail.empty()) {
            std::string d;
            for (auto& x : g_detail) d += x;
            LogPrintf("Hook: frame %u, draws of PS %08X [order kind count xinstances tex rt viewport]:%s", g_frameNo,
                      g_cfg.hookDetailPS, d.c_str());
        }
    }
    // Hunting one shader's draws: from HookHuntStartSec, skip its k-th draw every HookHuntSec
    if (g_cfg.hookMode == 2 && g_cfg.hookHuntDrawsOf && g_cfg.hookHuntSec > 0.0f) {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        const double t = (double)(now.QuadPart - g_t0.QuadPart) / (double)g_freq.QuadPart - g_cfg.hookHuntStartSec;
        int k = -1;
        if (t >= 0.0) {
            if (!g_lastOrdinals) g_lastOrdinals = g_ordinal;
            k = (int)(t / g_cfg.hookHuntSec);
            if (k >= (int)g_lastOrdinals) k = -1;
        }
        if (k != g_skipOrdinal) {
            if (k >= 0)
                LogPrintf("Hook: skipping draw %d of %u of PS %08X", k, g_lastOrdinals, g_cfg.hookHuntDrawsOf);
            else
                LogPrintf("Hook: skipping nothing");
            g_skipOrdinal = k;
        }
    }
    // Hunting one shader's textures: from HookHuntStartSec, skip the draws using its i-th texture
    if (g_cfg.hookMode == 2 && g_cfg.hookHuntTexOf && g_cfg.hookHuntSec > 0.0f) {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        const double t = (double)(now.QuadPart - g_t0.QuadPart) / (double)g_freq.QuadPart - g_cfg.hookHuntStartSec;
        int i = -1;
        if (t >= 0.0) {
            if (g_texHunt.empty()) {
                g_texHunt = g_texFrame;
                LogPrintf("Hook: hunting over %zu textures of PS %08X, %.1f s each", g_texHunt.size(),
                          g_cfg.hookHuntTexOf, g_cfg.hookHuntSec);
            }
            i = (int)(t / g_cfg.hookHuntSec);
            if (i >= (int)g_texHunt.size()) i = -1;
        }
        void* skip = i >= 0 ? g_texHunt[i].tex : nullptr;
        if (skip != g_skipTex) {
            if (skip)
                LogPrintf("Hook: skipping texture %p %s (#%d)", skip, g_texHunt[i].desc.c_str(), i);
            else
                LogPrintf("Hook: skipping nothing");
            g_skipTex = skip;
        }
    }
    g_texFrame.clear();
    g_targets.clear();
    g_ordinal = 0;
    g_reticleDraws = 0;
    g_detail.clear();
    // Hunting: from HookHuntStartSec, skip the draws of one pixel shader every HookHuntSec,
    // in the order the frame at the start drew them
    if (g_cfg.hookMode == 2 && !g_cfg.hookHuntDrawsOf && !g_cfg.hookHuntTexOf && g_cfg.hookHuntSec > 0.0f) {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        const double t = (double)(now.QuadPart - g_t0.QuadPart) / (double)g_freq.QuadPart - g_cfg.hookHuntStartSec;
        if (t >= 0.0 && g_huntList.empty()) {
            for (auto& g : g_frame) {
                bool dup = false;
                for (uint32_t p : g_huntList) dup |= p == g.ps;
                if (!dup) g_huntList.push_back(g.ps);
            }
            LogPrintf("Hook: hunting over %zu pixel shaders, %.1f s each", g_huntList.size(), g_cfg.hookHuntSec);
        }
        uint32_t skip = 0;
        int i = -1;
        if (t >= 0.0 && !g_huntList.empty()) {
            i = (int)(t / g_cfg.hookHuntSec);
            if (i < (int)g_huntList.size()) skip = g_huntList[i];
        }
        if (g_cfg.hookSkipPS) skip = g_cfg.hookSkipPS;
        if (skip != g_skipPS) {
            g_skipPS = skip;
            if (skip)
                LogPrintf("Hook: skipping PS %08X (#%d)", skip, i);
            else
                LogPrintf("Hook: skipping nothing");
        }
    }
    g_frame.clear();
    g_drawsGame = g_drawsOther = 0;
}
