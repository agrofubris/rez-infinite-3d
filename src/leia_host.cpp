#include "leia_host.h"
#include "log.h"

#include "../leia/rez3d_leia_sr.h"

#include <cstdlib>

template <class T> static void SafeRelease(T*& p) {
    if (p) { p->Release(); p = nullptr; }
}

void LeiaSrHost::Decline(const char* why) {
    if (!m_unavailable) LogPrintf("Leia SR: %s; presenting side-by-side", why);
    m_unavailable = true;
}

// rez3d_leia_sr.dll sits next to this openvr_api.dll
bool LeiaSrHost::LoadApi() {
    if (m_api) return true;
    if (m_unavailable) return false;
    char disable[8] = {};
    if (GetEnvironmentVariableA("REZ3D_DISABLE_LEIA_WEAVE", disable, sizeof(disable)) > 0 && disable[0] != '0') {
        Decline("weave disabled by REZ3D_DISABLE_LEIA_WEAVE");
        return false;
    }
    static const int anchor = 0;  // any address inside this DLL
    wchar_t path[MAX_PATH] = {};
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&anchor), &self);
    GetModuleFileNameW(self, path, MAX_PATH);
    wchar_t* slash = wcsrchr(path, L'\\');
    if (slash) slash[1] = 0;
    wcscat_s(path, L"rez3d_leia_sr.dll");
    m_module = LoadLibraryW(path);
    if (!m_module) {
        Decline("rez3d_leia_sr.dll not installed");
        return false;
    }
    using GetApi = const Rez3DLeiaSrApi* (*)();
    auto getApi = reinterpret_cast<GetApi>(GetProcAddress(m_module, "rez3d_leia_sr_get_api"));
    const Rez3DLeiaSrApi* api = getApi ? getApi() : nullptr;
    if (!api || api->version != REZ3D_LEIA_SR_API_VERSION) {
        Decline("rez3d_leia_sr.dll is a different version");
        return false;
    }
    m_api = api;
    return true;
}

bool LeiaSrHost::Ready(ID3D11Device* device, ID3D11DeviceContext* context, HWND window, int width, int height) {
    if (m_unavailable) return false;
    if (!window) {
        Decline("no window (headless)");
        return false;
    }
    width &= ~1;  // even extents: each half is a whole eye
    height &= ~1;
    if (!LoadApi()) return false;
    if (!m_weaverReady) {
        if (!m_api->init(context, window)) {
            Decline(m_api->status());
            return false;
        }
        m_weaverReady = true;
        LogPrintf("Leia SR: weaver %s, window %p", m_api->status(), (void*)window);
    }
    if (width != m_w || height != m_h || !m_pair) {
        ReleaseTextures();
        D3D11_TEXTURE2D_DESC td{};
        td.Width = width * 2;
        td.Height = height;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;  // the SDK's byte order: RGBA can weave black
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        HRESULT hr = device->CreateTexture2D(&td, nullptr, &m_pair);
        if (SUCCEEDED(hr)) hr = device->CreateRenderTargetView(m_pair, nullptr, &m_pairRtv);
        if (SUCCEEDED(hr)) hr = device->CreateShaderResourceView(m_pair, nullptr, &m_pairSrv);
        td.Width = width;
        td.BindFlags = D3D11_BIND_RENDER_TARGET;
        if (SUCCEEDED(hr)) hr = device->CreateTexture2D(&td, nullptr, &m_out);
        if (SUCCEEDED(hr)) hr = device->CreateRenderTargetView(m_out, nullptr, &m_outRtv);
        if (FAILED(hr)) {
            ReleaseTextures();
            Release();
            Decline("could not create the pair textures");
            return false;
        }
        m_w = width;
        m_h = height;
        m_settle = true;  // let the panel settle for one frame after a (re)size
        LogPrintf("Leia SR: pair %dx%d, woven output %dx%d", width * 2, height, width, height);
    }
    if (m_settle) {
        m_settle = false;
        return false;
    }
    return true;
}

bool LeiaSrHost::WeaveInto(ID3D11DeviceContext* context, ID3D11Texture2D* backBuffer) {
    if (!m_api || !m_weaverReady || !m_outRtv || !backBuffer) return false;
    // Weave into our own target, never straight into the swapchain
    const float black[4] = { 0, 0, 0, 1 };
    context->ClearRenderTargetView(m_outRtv, black);
    context->OMSetRenderTargets(1, &m_outRtv, nullptr);
    D3D11_VIEWPORT vp{ 0.0f, 0.0f, (float)m_w, (float)m_h, 0.0f, 1.0f };
    context->RSSetViewports(1, &vp);
    const bool ok = m_api->weave(m_pairSrv) != 0;
    // The weaver binds its own shaders and states; hand the presenter a clean
    // context (it sets everything it uses each frame)
    context->ClearState();
    if (!ok) {
        LogPrintf("Leia SR: %s; presenting side-by-side", m_api->status());
        Release();
        m_unavailable = true;
        return false;
    }
    D3D11_TEXTURE2D_DESC bd{};
    backBuffer->GetDesc(&bd);
    if ((int)bd.Width == m_w && (int)bd.Height == m_h)
        context->CopyResource(backBuffer, m_out);
    else
        context->CopySubresourceRegion(backBuffer, 0, 0, 0, 0, m_out, 0, nullptr);
    if (!m_loggedWeave) {
        m_loggedWeave = true;
        LogPrintf("Leia SR: wove %dx%d", m_w, m_h);
    }
    return true;
}

void LeiaSrHost::ReleaseTextures() {
    SafeRelease(m_outRtv);
    SafeRelease(m_out);
    SafeRelease(m_pairSrv);
    SafeRelease(m_pairRtv);
    SafeRelease(m_pair);
    m_w = m_h = 0;
}

// Must run before the device and the textures the weaver sampled go away
void LeiaSrHost::Release() {
    if (m_api && m_weaverReady) m_api->shutdown();
    m_weaverReady = false;
    ReleaseTextures();
}
