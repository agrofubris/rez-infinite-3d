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

void Fail(const char* what, HRESULT hr) {
    g_failed = true;
    LogPrintf("Reticle shape: %s failed (0x%08lX), the reticle keeps the game's shape and size", what, (unsigned long)hr);
}

// Each triangle of a reticle quad, redrawn by RsReshape (reticle_vs.cpp)
bool BuildShader(ID3D11Device* dev) {
    std::string fields, pos, uv, error;
    if (!ReticleVSFields(fields, pos, uv, error)) {
        Fail(error.c_str(), E_FAIL);
        return false;
    }
    std::string body =
        "[maxvertexcount(3)]\n"
        "void main(triangle V i[3], inout TriangleStream<V> o) {\n"
        "    V v0 = i[0], v1 = i[1], v2 = i[2];\n"
        "    RsReshape(v0.POS, v1.POS, v2.POS, " +
        ReticleUV(uv, "v0") + ", " + ReticleUV(uv, "v1") + ", " + ReticleUV(uv, "v2") +
        ");\n"
        "    o.Append(v0);\n"
        "    o.Append(v1);\n"
        "    o.Append(v2);\n"
        "}\n";
    for (size_t at; (at = body.find("POS")) != std::string::npos;) body.replace(at, 3, pos);
    const std::string src = "struct V {\n" + fields + "};\n" + ReticleShapeHLSL() + body;
    ID3DBlob* code = nullptr;
    ID3DBlob* errors = nullptr;
    HRESULT hr = D3DCompile(src.c_str(), src.size(), "reticle_scale", nullptr, nullptr, "main", "gs_5_0",
                            D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
    if (FAILED(hr)) {
        LogPrintf("Reticle shape: shader source:\n%s", src.c_str());
        if (errors) LogPrintf("Reticle shape: %s", (const char*)errors->GetBufferPointer());
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
    LogPrintf("Reticle shape: geometry shader ready");
    return true;
}

}  // namespace

void ReticleScaleDraw(ID3D11DeviceContext* ctx, const ReticleShape& shape, const std::function<void()>& draw) {
    {
        std::lock_guard<std::mutex> lk(g_lock);
        bool ready = !g_failed;
        if (ready && !g_gs) {
            ID3D11Device* dev = nullptr;
            ctx->GetDevice(&dev);
            ready = dev && BuildShader(dev);
            if (dev) dev->Release();
        }
        if (!ready) {
            draw();
            return;
        }
    }
    ID3D11Buffer* cb = nullptr;
    if (!ReticleShapeBind(ctx, shape, &cb)) {
        draw();
        return;
    }
    ID3D11GeometryShader* gs = nullptr;
    ID3D11ClassInstance* inst[256];
    UINT ninst = 256;
    ctx->GSGetShader(&gs, inst, &ninst);
    ctx->GSSetShader(g_gs, nullptr, 0);
    draw();
    ctx->GSSetShader(gs, inst, ninst);
    ReticleShapeRestore(ctx, cb);
    if (gs) gs->Release();
    for (UINT i = 0; i < ninst; i++)
        if (inst[i]) inst[i]->Release();
}
