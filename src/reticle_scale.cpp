#include "reticle_scale.h"

#include <d3dcompiler.h>
#include <windows.h>

#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

#include "log.h"

namespace {

std::mutex g_lock;
std::vector<unsigned char> g_vsCode;
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
    ID3D11ShaderReflection* refl = nullptr;
    HRESULT hr = D3DReflect(g_vsCode.data(), g_vsCode.size(), __uuidof(ID3D11ShaderReflection), (void**)&refl);
    if (FAILED(hr)) {
        Fail("reflecting the reticle's vertex shader", hr);
        return false;
    }
    D3D11_SHADER_DESC sd{};
    refl->GetDesc(&sd);
    std::string fields, pos;
    for (UINT i = 0; i < sd.OutputParameters; i++) {
        D3D11_SIGNATURE_PARAMETER_DESC p{};
        refl->GetOutputParameterDesc(i, &p);
        int n = 0;
        for (int b = 0; b < 4; b++) n += (p.Mask >> b) & 1;
        const char* type = p.ComponentType == D3D_REGISTER_COMPONENT_UINT32   ? "uint"
                           : p.ComponentType == D3D_REGISTER_COMPONENT_SINT32 ? "int"
                                                                              : "float";
        char name[16], line[160];
        sprintf_s(name, "v%u", i);
        sprintf_s(line, "    %s%d %s : %s%u;\n", type, n, name, p.SemanticName, p.SemanticIndex);
        if (p.SystemValueType == D3D_NAME_POSITION) {
            sprintf_s(line, "    float4 %s : SV_Position;\n", name);
            pos = name;
        }
        fields += line;
    }
    refl->Release();
    if (pos.empty()) {
        Fail("finding the vertex shader's position output", E_FAIL);
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
    hr = D3DCompile(src.c_str(), src.size(), "reticle_scale", nullptr, nullptr, "main", "gs_5_0",
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

void ReticleScaleSetVSCode(const void* code, size_t size) {
    std::lock_guard<std::mutex> lk(g_lock);
    if (!g_vsCode.empty()) return;
    const unsigned char* p = static_cast<const unsigned char*>(code);
    g_vsCode.assign(p, p + size);
}

void ReticleScaleDraw(ID3D11DeviceContext* ctx, float scale, const std::function<void()>& draw) {
    {
        std::lock_guard<std::mutex> lk(g_lock);
        bool ready = !g_failed;
        if (ready && g_vsCode.empty()) {
            Fail("finding the reticle's vertex shader bytecode", E_FAIL);
            ready = false;
        }
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
