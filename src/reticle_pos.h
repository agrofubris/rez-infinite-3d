#pragma once

#include <d3d11.h>

#include <functional>

#include "reticle_state.h"

// Measures where Rez's reticle is shown, per eye, on the GPU: at the octagon's draw, a
// geometry shader of ours works out the quad's centre and half-size in clip space (on
// screen or not) and our pixel shader writes them to a buffer, read back a frame or two
// later (no stall).

// At the octagon's draw, with the game's state bound; draw() issues that draw call. shiftNdc
// is how far our reticle depth moves it sideways in this eye, scale how much bigger we draw it.
void ReticlePosMeasure(ID3D11DeviceContext* ctx, int eye, float shiftNdc, float scale,
                       const std::function<void()>& draw);

// The frustum, for the bridge (set once at init)
void ReticlePosSetProjection(float tanH, float tanV, float shiftV);

// The latest measurement of both eyes; 0 until there is one
int ReticlePosLatest(Rez3DReticleState* out);
