#pragma once

#include <d3d11.h>

#include <functional>

#include "reticle_vs.h"

// Redraws Rez's reticle in its shape (reticle_vs.h): flat, facing the view, and/or larger or
// smaller, always around each quad's own centre, so it stays where the game aims it and at
// the same depth.

// At the reticle's draw, with the game's state bound; draw() issues that draw call.
void ReticleScaleDraw(ID3D11DeviceContext* ctx, const ReticleShape& shape, const std::function<void()>& draw);
