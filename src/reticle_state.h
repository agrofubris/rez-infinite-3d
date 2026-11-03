#pragma once

// What the 3D runtime (openvr_api.dll) measures of Rez's reticle, for the controller bridge
// (xinput1_3.dll) in the same process. The bridge gets it from openvr_api.dll's export
// Rez3DReticleState(Rez3DReticleState*), which returns 0 until there is a measurement.

const int kRez3DReticleStateVersion = 1;

struct Rez3DReticleState {
    int version;      // kRez3DReticleStateVersion
    int sequence;     // counts measurements; unchanged = nothing new
    double drawnAgo;  // seconds since the measured frame drew the reticle
    // Per eye, where the reticle's octagon is shown, in that eye's normalized device
    // coordinates: -1..1 spans its picture, +y is up. The centre, and its half-size.
    float x[2], y[2];
    float halfX[2], halfY[2];
    // The eye picture's frustum: half-width and half-height as tangents, and how far lens
    // shift moves its middle (tangent units, OpenVR's y-down convention, as in rez3d.log).
    float tanH, tanV, shiftV;
};

typedef int (*Rez3DReticleStateFn)(Rez3DReticleState* out);
