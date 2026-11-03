#pragma once

#include <d3d11.h>

#include <functional>

#include "reticle_state.h"
#include "reticle_vs.h"

// Measures where Rez's reticle is shown, per eye, on the GPU: at the octagon's draw, a
// geometry shader of ours works out the quad's centre and half-size in clip space (on
// screen or not) and our pixel shader writes them to a buffer, read back a frame or two
// later (no stall).

// At the octagon's draw, with the game's state bound; draw() issues that draw call. shiftNdc
// is how far our reticle depth moves it sideways in this eye, shape how we draw it.
void ReticlePosMeasure(ID3D11DeviceContext* ctx, int eye, float shiftNdc, const ReticleShape& shape,
                       const std::function<void()>& draw);

// The frustum, for the bridge and the reticle's shape (set once at init)
void ReticlePosSetProjection(float tanH, float tanV, float shiftV);
void ReticlePosProjection(float& tanH, float& tanV, float& shiftV);

// The latest measurement of both eyes; 0 until there is one
int ReticlePosLatest(Rez3DReticleState* out);

// Where the game aims in this eye (the octagon's centre before our depth shift) and the
// octagon's half-size, in NDC; false until measured
bool ReticlePosAim(int eye, float& x, float& y, float& halfX, float& halfY);
