#pragma once

#include <d3d11.h>

#include <cstdint>
#include <functional>

// What Rez draws under the middle of its reticle, and how near: most of Rez's enemies and
// effects are glowing, see-through and never write the depth buffer, so the depth buffer
// mostly holds nothing there. Instead, each game draw into an eye image (with the reticle's
// vertex shader) is drawn once more by a pixel shader of ours, clipped to a small box at the
// middle of the reticle (where it was last measured), which keeps the nearest depth that its
// visible pixels reach. Read back a frame or two later (no stall).

// Before a game draw into this eye: draw() issues that draw call. ps / verts identify it (log).
void ReticleProbeDraw(ID3D11DeviceContext* ctx, int eye, uint32_t ps, unsigned verts,
                      const std::function<void()>& draw);

// At the reticle's octagon in this eye (late in the frame): this frame's results go to be read
void ReticleProbeEnd(ID3D11DeviceContext* ctx, int eye);

// A new frame starts (both eyes are in)
void ReticleProbeFrame();

// Ignores whatever is nearer than this depth value (reversed: larger = nearer; 0 = nothing ignored)
void ReticleProbeSetNearLimit(float depth);

// Testing: log what reached the middle every this many results per eye (default 120)
void ReticleProbeSetLogEvery(int results);

// The latest nearest depth (reversed: larger = nearer, 0 = nothing) under the middle of the
// reticle over both eyes, and how many draws reached there. False until a result is back.
bool ReticleProbeLatest(float& nearest, unsigned& draws);
