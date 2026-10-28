#pragma once

#include <windows.h>
#include <d3d11.h>

struct Rez3DLeiaSrApi;

// Format=LeiaSR on the presenter side. Loads rez3d_leia_sr.dll lazily, owns the
// full-resolution side-by-side pair (2W x H) and the woven target (W x H), and
// latches "unavailable" after the first failure so nothing is retried per frame.
// Every decline means the caller presents the plain side-by-side image.
class LeiaSrHost {
public:
    // True when this frame should be composed into PairRtv() and woven.
    // Creates the textures and the weaver on first use.
    bool Ready(ID3D11Device* device, ID3D11DeviceContext* context, HWND window, int width, int height);
    ID3D11RenderTargetView* PairRtv() const { return m_pairRtv; }
    ID3D11Texture2D* PairTexture() const { return m_pair; }
    // Weaves the pair into the owned target and copies it into backBuffer.
    // On failure the session is disabled and false is returned.
    bool WeaveInto(ID3D11DeviceContext* context, ID3D11Texture2D* backBuffer);
    void Release();
    bool Unavailable() const { return m_unavailable; }

private:
    bool LoadApi();
    void Decline(const char* why);
    void ReleaseTextures();

    HMODULE m_module = nullptr;
    const Rez3DLeiaSrApi* m_api = nullptr;
    bool m_unavailable = false;
    bool m_weaverReady = false;
    bool m_settle = false;
    bool m_loggedWeave = false;
    int m_w = 0, m_h = 0;
    ID3D11Texture2D* m_pair = nullptr;
    ID3D11RenderTargetView* m_pairRtv = nullptr;
    ID3D11ShaderResourceView* m_pairSrv = nullptr;
    ID3D11Texture2D* m_out = nullptr;
    ID3D11RenderTargetView* m_outRtv = nullptr;
};
