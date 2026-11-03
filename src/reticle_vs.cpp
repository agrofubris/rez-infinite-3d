#include "reticle_vs.h"

#include <d3dcompiler.h>
#include <windows.h>

#include <cstdio>
#include <mutex>

#include "log.h"

namespace {
std::mutex g_lock;
std::vector<unsigned char> g_code;
bool g_logged = false;
}  // namespace

void ReticleVSSetCode(const void* code, size_t size) {
    std::lock_guard<std::mutex> lk(g_lock);
    if (!g_code.empty()) return;
    const unsigned char* p = static_cast<const unsigned char*>(code);
    g_code.assign(p, p + size);
}

bool ReticleVSFields(std::string& fields, std::string& pos, std::string& uv, std::string& error) {
    std::lock_guard<std::mutex> lk(g_lock);
    fields.clear();
    pos.clear();
    uv.clear();
    if (g_code.empty()) {
        error = "finding the reticle's vertex shader bytecode";
        return false;
    }
    ID3D11ShaderReflection* refl = nullptr;
    if (FAILED(D3DReflect(g_code.data(), g_code.size(), __uuidof(ID3D11ShaderReflection), (void**)&refl))) {
        error = "reflecting the reticle's vertex shader";
        return false;
    }
    D3D11_SHADER_DESC sd{};
    refl->GetDesc(&sd);
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
        } else if (uv.empty() && _stricmp(p.SemanticName, "TEXCOORD") == 0 && n >= 2 &&
                   p.ComponentType == D3D_REGISTER_COMPONENT_FLOAT32) {
            uv = "i." + std::string(name) + ".xy";
        }
        fields += line;
        if (!g_logged)
            LogPrintf("Reticle: vertex shader output %u: %s%u reg %u mask 0x%X %s%d", i, p.SemanticName,
                      p.SemanticIndex, p.Register, p.Mask, type, n);
    }
    g_logged = true;
    refl->Release();
    if (pos.empty()) {
        error = "finding the vertex shader's position output";
        return false;
    }
    return true;
}
