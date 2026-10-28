#pragma once

// rez3d_leia_sr.dll C ABI. openvr_api.dll never links this DLL: it loads it at
// runtime (only for Format=LeiaSR) and resolves rez3d_leia_sr_get_api. All SR
// code (SDK headers, import libraries, delay-loads) lives on the DLL side, so
// the game starts normally on machines without the SR Platform.
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define REZ3D_LEIA_SR_API_VERSION 1

typedef struct Rez3DLeiaSrApi {
    uint32_t version;
    // d3d11Context: ID3D11DeviceContext* (immediate). hwnd: the window the
    // weave is shown in. Returns 1 when the weaver is ready, 0 when the SR
    // runtime, service or display is unavailable (call once; the host latches).
    int (*init)(void* d3d11Context, void* hwnd);
    // Weaves the side-by-side pair (ID3D11ShaderResourceView*, left eye in the
    // left half, each half a full-resolution eye) into the render target and
    // viewport currently bound on the context. Returns 1 woven, 0 failed (the
    // session is then disabled).
    int (*weave)(void* pairSrv);
    void (*shutdown)(void);
    // Short human-readable state for the log (never NULL)
    const char* (*status)(void);
} Rez3DLeiaSrApi;

#if defined(REZ3D_LEIA_SR_BUILDING)
#define REZ3D_LEIA_SR_EXPORT __declspec(dllexport)
#else
#define REZ3D_LEIA_SR_EXPORT __declspec(dllimport)
#endif

REZ3D_LEIA_SR_EXPORT const Rez3DLeiaSrApi* rez3d_leia_sr_get_api(void);

#ifdef __cplusplus
}
#endif
