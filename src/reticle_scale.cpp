#include "reticle_scale.h"

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
ID3D11GeometryShader* g_gs = nullptr;
float g_gsScale = 0.0f;  // the scale g_gs was built for

void Fail(const char* what, HRESULT hr) {
    g_failed = true;
    LogPrintf("Reticle size: %s failed (0x%08lX), the reticle keeps the game's size", what, (unsigned long)hr);
}

// The quad's corners are clip-space positions, which are linear in the 3D position: the
// middle of a triangle's longest side (the quad's diagonal, which both its triangles share)
// is the quad's centre, and scaling around it in clip space scales the quad in 3D.
bool BuildShader(ID3D11Device* dev, float scale) {
    std::string fields, pos, uv, error;
    if (!ReticleVSFields(fields, pos, uv, error)) {
        Fail(error.c_str(), E_FAIL);
        return false;
    }
    char scaleText[32];
    sprintf_s(scaleText, "%.4f", scale);
    std::string body =
        "[maxvertexcount(3)]\n"
        "void main(triangle V i[3], inout TriangleStream<V> o) {\n"
        "    float2 s0 = i[0].POS.xy / i[0].POS.w, s1 = i[1].POS.xy / i[1].POS.w, s2 = i[2].POS.xy / i[2].POS.w;\n"
        "    float d01 = dot(s1 - s0, s1 - s0), d12 = dot(s2 - s1, s2 - s1), d20 = dot(s0 - s2, s0 - s2);\n"
        "    float4 c = (d01 >= d12 && d01 >= d20) ? i[0].POS + i[1].POS\n"
        "             : (d12 >= d20) ? i[1].POS + i[2].POS : i[2].POS + i[0].POS;\n"
        "    c *= 0.5;\n"
        "    [unroll] for (int k = 0; k < 3; k++) {\n"
        "        V v = i[k];\n"
        "        v.POS = c + (v.POS - c) * SCALE;\n"
        "        o.Append(v);\n"
        "    }\n"
        "}\n";
    for (size_t at; (at = body.find("POS")) != std::string::npos;) body.replace(at, 3, pos);
    body.replace(body.find("SCALE"), 5, scaleText);
    const std::string src = "struct V {\n" + fields + "};\n" + body;
    ID3DBlob* code = nullptr;
    ID3DBlob* errors = nullptr;
    HRESULT hr = D3DCompile(src.c_str(), src.size(), "reticle_scale", nullptr, nullptr, "main", "gs_5_0",
                    D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
    if (FAILED(hr)) {
        LogPrintf("Reticle size: shader source:\n%s", src.c_str());
        if (errors) LogPrintf("Reticle size: %s", (const char*)errors->GetBufferPointer());
        if (errors) errors->Release();
        Fail("compiling our geometry shader", hr);
        return false;
    }
    if (errors) errors->Release();
    hr = dev->CreateGeometryShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &g_gs);
    code->Release();
    if (FAILED(hr)) {
        Fail("creating our geometry shader", hr);
        return false;
    }
    g_gsScale = scale;
    LogPrintf("Reticle size: geometry shader ready (x%.2f)", scale);
    return true;
}

}  // namespace

void ReticleScaleDraw(ID3D11DeviceContext* ctx, float scale, const std::function<void()>& draw) {
    {
        std::lock_guard<std::mutex> lk(g_lock);
        bool ready = !g_failed;
        if (ready && (!g_gs || g_gsScale != scale)) {
            if (g_gs) g_gs->Release();
            g_gs = nullptr;
            ID3D11Device* dev = nullptr;
            ctx->GetDevice(&dev);
            ready = dev && BuildShader(dev, scale);
            if (dev) dev->Release();
        }
        if (!ready) {
            draw();
            return;
        }
    }
    ID3D11GeometryShader* gs = nullptr;
    ID3D11ClassInstance* inst[256];
    UINT ninst = 256;
    ctx->GSGetShader(&gs, inst, &ninst);
    ctx->GSSetShader(g_gs, nullptr, 0);
    draw();
    ctx->GSSetShader(gs, inst, ninst);
    if (gs) gs->Release();
    for (UINT i = 0; i < ninst; i++)
        if (inst[i]) inst[i]->Release();
}
