// Optional loader DLL: rez3d_leia_sr.dll, the only part of Rez Infinite 3D
// that touches the Leia SR SDK. It links SR-lib (bo3b, MIT) and delay-loads
// every SR runtime DLL, so it loads anywhere and simply reports "unavailable"
// on machines without the SR Platform.
#include "rez3d_leia_sr.h"

#include <windows.h>
#include <d3d11.h>

#include <exception>
#include <mutex>
#include <string>

#include "SR.hpp"

namespace {

std::mutex g_mutex;
SimulatedReality::SRInterfaceDX11* g_weaver = nullptr;
bool g_lensHint = false;
std::string g_status = "not started";

void DestroyLocked() {
    if (g_lensHint) {
        try { SimulatedReality::SRDisableLensHint(); } catch (...) {}
        g_lensHint = false;
    }
    if (g_weaver) {
        // Delete() also releases the SR context; the SDK can throw if the
        // device went away underneath it
        try { g_weaver->Delete(); } catch (...) {}
        g_weaver = nullptr;
    }
}

int ApiInit(void* d3d11Context, void* hwnd) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_weaver) return 1;
    if (!d3d11Context || !hwnd) {
        g_status = "unavailable: no device context or window";
        return 0;
    }
    try {
        SimulatedReality::SRInterfaceDX11* weaver = nullptr;
        // Probes the SR DLLs with LoadLibrary first and returns E_NOINTERFACE
        // (0x80004002) when the runtime, service or display is missing
        HRESULT hr = SimulatedReality::CreateSRInterfaceDX11(static_cast<ID3D11DeviceContext*>(d3d11Context),
                                                            static_cast<HWND>(hwnd), &weaver);
        if (FAILED(hr) || !weaver) {
            char buf[96];
            sprintf_s(buf, "unavailable: CreateSRInterfaceDX11 hr=0x%08lX", (unsigned long)hr);
            g_status = buf;
            return 0;
        }
        g_weaver = weaver;
        // The compositor already encoded gamma into a UNORM pair: no conversion
        g_weaver->SetShaderSRGBConversion(false, false);
        // Ask the panel to switch its lenses to 3D while we weave
        g_lensHint = SUCCEEDED(SimulatedReality::SREnableLensHint());
        g_status = g_lensHint ? "ready (lens hint on)" : "ready (lens hint unavailable)";
        return 1;
    } catch (const std::exception& e) {
        g_status = std::string("unavailable: ") + e.what();
    } catch (...) {
        g_status = "unavailable: exception during init";
    }
    DestroyLocked();
    return 0;
}

int ApiWeave(void* pairSrv) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_weaver || !pairSrv) return 0;
    try {
        // Set every frame: the weaver reads size and format from the view
        g_weaver->SetInputTexture(static_cast<ID3D11ShaderResourceView*>(pairSrv));
        g_weaver->Weave();
        return 1;
    } catch (const std::exception& e) {
        g_status = std::string("disabled: weave failed: ") + e.what();
    } catch (...) {
        g_status = "disabled: weave failed";
    }
    DestroyLocked();
    return 0;
}

void ApiShutdown(void) {
    std::lock_guard<std::mutex> lock(g_mutex);
    DestroyLocked();
    g_status = "shut down";
}

const char* ApiStatus(void) {
    static char buffer[256];
    std::lock_guard<std::mutex> lock(g_mutex);
    strncpy_s(buffer, g_status.c_str(), _TRUNCATE);
    return buffer;
}

const Rez3DLeiaSrApi g_api = { REZ3D_LEIA_SR_API_VERSION, ApiInit, ApiWeave, ApiShutdown, ApiStatus };

}  // namespace

const Rez3DLeiaSrApi* rez3d_leia_sr_get_api(void) { return &g_api; }
