#pragma once

#include <d3d11.h>

#include <functional>

// Measures the scene's depth under Rez's reticle, per eye, on the GPU: at the reticle's draw
// it copies the bound depth buffer, draws the reticle's quad once more with a pixel shader of
// ours that collects the depth under the quad's middle, and reads the result back a couple of
// frames later (no stall).

// The reticle's vertex shader bytecode (kept at creation): our pixel shader's inputs must
// match its outputs.
void ReticleDepthSetVSCode(const void* code, size_t size);

// At the reticle's draw, with the game's state bound; draw() issues that draw call again.
void ReticleDepthMeasure(ID3D11DeviceContext* ctx, int eye, const std::function<void()>& draw);

// Testing: "x,y,r" samples a grid over that box of the depth buffer (pixels) instead of
// under the reticle
void ReticleDepthSetProbe(const char* box);

// The latest result for an eye: nearest and farthest raw depth values and how many pixels
// were sampled. False until a result has come back.
bool ReticleDepthLatest(int eye, float& nearest, float& farthest, unsigned& samples);
