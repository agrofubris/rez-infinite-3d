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
