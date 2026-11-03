#pragma once

#include <d3d11.h>

#include <functional>

// Draws Rez's reticle larger or smaller: a geometry shader of ours, built from the reticle
// vertex shader's outputs, scales each of its quads around the quad's centre, so the
// reticle grows in place and stays where the game aims it.

// The reticle's vertex shader bytecode (kept at creation): our geometry shader's inputs and
// outputs must match its outputs.
void ReticleScaleSetVSCode(const void* code, size_t size);

// At the reticle's draw, with the game's state bound; draw() issues that draw call.
void ReticleScaleDraw(ID3D11DeviceContext* ctx, float scale, const std::function<void()>& draw);
