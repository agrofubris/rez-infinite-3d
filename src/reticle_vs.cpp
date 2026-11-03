#include "reticle_vs.h"

#include <d3d11.h>
#include <d3dcompiler.h>
#include <windows.h>

#include <cstdio>
#include <cstring>
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

// The quad's corners are clip-space positions, which are linear in the 3D position: the middle
// of the quad's diagonal, which both its triangles share, is the quad's centre. The diagonal is
// the side whose ends differ in both texture coordinates (a wide view can shear the quad
// enough that a side is longer than it on screen); without texture coordinates, the longest
// side. Scaling around the centre in clip space scales the quad in 3D. Flat: the corners go
// back to view space (undoing this eye's frustum), the quad is turned to straight ahead (yaw,
// then pitch, as a quad facing the eye is built), projected there, and drawn at its real
// centre and depth. So it looks the same anywhere on screen, at the size it has in the middle
// (a flat picture of a wide view stretches whatever is off-centre).
const char* ReticleShapeHLSL() {
    return R"(
cbuffer ReticleShape : register(b0) {
    float4 rsProj;  // tanH, tanV, shiftX, shiftY
    float4 rsOpts;  // scale, flat
};
float RsSide(float4 a, float4 b, float2 ta, float2 tb) {
    float2 sa = a.xy / a.w, sb = b.xy / b.w;
    float2 dt = abs(tb - ta);
    return min(dt.x, dt.y) * 100.0 + dot(sb - sa, sb - sa);
}
float4 RsCenter(float4 p0, float4 p1, float4 p2, float2 t0, float2 t1, float2 t2) {
    float d01 = RsSide(p0, p1, t0, t1), d12 = RsSide(p1, p2, t1, t2), d20 = RsSide(p2, p0, t2, t0);
    return 0.5 * ((d01 >= d12 && d01 >= d20) ? p0 + p1 : (d12 >= d20) ? p1 + p2 : p2 + p0);
}
float3 RsView(float4 p) {
    return float3(rsProj.x * p.x + rsProj.z * p.w, rsProj.y * p.y + rsProj.w * p.w, -p.w);
}
float4 RsFlat(float4 p, float4 c, float cy, float sy, float cp, float sp) {
    float3 v = RsView(p);
    v = float3(v.x * cy + v.z * sy, v.y, -v.x * sy + v.z * cy);
    v = float3(v.x, v.y * cp - v.z * sp, v.y * sp + v.z * cp);
    float2 n = float2(v.x / (-v.z) / rsProj.x, v.y / (-v.z) / rsProj.y) * rsOpts.x;
    return float4((c.xy / c.w + n) * c.w, c.z, c.w);
}
void RsReshape(inout float4 p0, inout float4 p1, inout float4 p2, float2 t0, float2 t1, float2 t2) {
    float4 c = RsCenter(p0, p1, p2, t0, t1, t2);
    if (rsOpts.y > 0.5 && c.w > 0) {
        float3 d = normalize(RsView(c));
        float yaw = atan2(d.x, -d.z), pitch = asin(clamp(d.y, -1.0, 1.0));
        float cy = cos(yaw), sy = sin(yaw), cp = cos(-pitch), sp = sin(-pitch);
        p0 = RsFlat(p0, c, cy, sy, cp, sp);
        p1 = RsFlat(p1, c, cy, sy, cp, sp);
        p2 = RsFlat(p2, c, cy, sy, cp, sp);
    } else {
        p0 = c + (p0 - c) * rsOpts.x;
        p1 = c + (p1 - c) * rsOpts.x;
        p2 = c + (p2 - c) * rsOpts.x;
    }
}
)";
}

namespace {
std::mutex g_shapeLock;
ID3D11Device* g_shapeDev = nullptr;
ID3D11Buffer* g_shapeBuf = nullptr;
}  // namespace

bool ReticleShapeBind(ID3D11DeviceContext* ctx, const ReticleShape& s, ID3D11Buffer** previous) {
    *previous = nullptr;
    std::lock_guard<std::mutex> lk(g_shapeLock);
    ID3D11Device* dev = nullptr;
    ctx->GetDevice(&dev);
    if (!dev) return false;
    if (dev != g_shapeDev) {
        if (g_shapeBuf) g_shapeBuf->Release();
        g_shapeBuf = nullptr;
        D3D11_BUFFER_DESC b{};
        b.ByteWidth = sizeof(ReticleShape);
        b.Usage = D3D11_USAGE_DYNAMIC;
        b.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        b.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(dev->CreateBuffer(&b, nullptr, &g_shapeBuf))) {
            g_shapeBuf = nullptr;
            LogPrintf("Reticle: creating the shape constants failed");
        }
        g_shapeDev = dev;
    }
    dev->Release();
    if (!g_shapeBuf) return false;
    D3D11_MAPPED_SUBRESOURCE m{};
    if (FAILED(ctx->Map(g_shapeBuf, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return false;
    memcpy(m.pData, &s, sizeof(s));
    ctx->Unmap(g_shapeBuf, 0);
    ctx->GSGetConstantBuffers(0, 1, previous);
    ctx->GSSetConstantBuffers(0, 1, &g_shapeBuf);
    return true;
}

void ReticleShapeRestore(ID3D11DeviceContext* ctx, ID3D11Buffer* previous) {
    ctx->GSSetConstantBuffers(0, 1, &previous);
    if (previous) previous->Release();
}

std::string ReticleUV(const std::string& uv, const char* var) {
    // ReticleVSFields gives it as "i.<field>.xy"
    if (uv.size() < 3) return "float2(0, 0)";
    return std::string(var) + uv.substr(1);
}
