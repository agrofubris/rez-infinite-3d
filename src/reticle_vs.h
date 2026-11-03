#pragma once

#include <cstddef>
#include <string>
#include <vector>

// Rez's reticle vertex shader, kept at creation: the shaders we draw the reticle's quads with
// (depth, size, position) must match its outputs.

void ReticleVSSetCode(const void* code, size_t size);

// Its outputs as HLSL struct fields ("float4 v0 : SV_Position;" ...), the name of the position
// field and, if there is one, the quad's texture coordinates ("i.v1.xy"). False (logged by the
// caller) when there is no bytecode or no position output.
bool ReticleVSFields(std::string& fields, std::string& pos, std::string& uv, std::string& error);

// How the reticle's quads are drawn, per draw (its eye): read by our geometry shaders
struct ID3D11Buffer;
struct ID3D11DeviceContext;
struct ReticleShape {
    float tanH, tanV;      // the eye frustum's half-width and half-height, as tangents
    float shiftX, shiftY;  // where this eye's frustum is centred (tangents): eye shift, lens shift
    float scale;           // size, x the game's (around the quad's centre)
    float flat;            // 1 = facing the view, as it would look in the middle of the picture
    float pad[2];
};

// HLSL: the shape constants (register b0), RsCenter(p0, p1, p2, t0, t1, t2), the centre of the
// quad a triangle belongs to (clip-space corners and their texture coordinates), and
// RsReshape(inout p0, p1, p2, t0, t1, t2), which redraws the triangle as the shape says
const char* ReticleShapeHLSL();

// The texture coordinates of the corner in variable `var` (an instance of the vertex shader's
// output struct): its first float2 TEXCOORD, or zeros (then the centre comes from lengths)
std::string ReticleUV(const std::string& uv, const char* var);

// Binds the shape constants to the geometry shader stage; returns what was bound there
// (pass it to ReticleShapeRestore). Null and nothing bound if the buffer can't be made.
bool ReticleShapeBind(ID3D11DeviceContext* ctx, const ReticleShape& s, ID3D11Buffer** previous);
void ReticleShapeRestore(ID3D11DeviceContext* ctx, ID3D11Buffer* previous);
