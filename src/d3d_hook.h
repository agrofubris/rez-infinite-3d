#pragma once

#include <d3d11.h>

#include "config.h"

// Hooks the game's own D3D11 device and immediate context (HookMode in rez3d.ini).
// For now a test aid: it logs the draw calls of a frame and can skip the draws of
// one pixel shader at a time, to find the reticle's draw.
void InstallD3DHooks(ID3D11Device* dev, ID3D11DeviceContext* ctx, const StereoConfig& cfg);
void InstallD3DHooksEarly(const StereoConfig& cfg);  // at VR init, before the game makes its shaders
void D3DHookFrame();
void D3DHookSetOwnDevice(ID3D11Device* dev);
void D3DHookNoteEye(int eye, ID3D11Texture2D* tex);  // which eye each submitted texture is
// The live stereo settings: each eye's offset at infinity (NDC units) and the convergence
void D3DHookSetStereo(float separationNdc, float convergence);  // the presenter's device, which the hooks leave alone  // once per game frame, after the right eye is submitted
