#pragma once

#include "config.h"
#include "leia_host.h"

#include <windows.h>
#include <d3d11.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct EyeBounds {
    float uMin = 0.0f, vMin = 0.0f, uMax = 1.0f, vMax = 1.0f;
};

// Returns false if the monitor index is out of range.
bool GetMonitorInfoByIndex(int index, RECT& rect, int& nativeW, int& nativeH);

// Output resolution and monitor rect for the given config.
void GetOutputSize(const StereoConfig& cfg, int& w, int& h, RECT& rect);

// Per-eye render target size for the configured stereo format.
void ComputeEyeSize(const StereoConfig& cfg, int outW, int outH, int& eyeW, int& eyeH);

// Physical width of the configured monitor from its EDID in mm (0 if unknown).
int GetMonitorWidthMM(const StereoConfig& cfg);

// Current refresh rate of the configured monitor in Hz (0 if unknown).
int GetMonitorRefreshHz(const StereoConfig& cfg);

class Presenter {
public:
    HWND LauncherWindow() const { return m_launcherHwnd.load(); }
    static Presenter& Get();

    void Configure(const StereoConfig& cfg);
    void Start();
    void Stop();
    void StopForProcessExit();

    // eye: 0=left, 1=right. texture is the submitted D3D11 texture.
    void SubmitEye(int eye, ID3D11Texture2D* texture, const EyeBounds& bounds, int submitFlags);

    // Frame pacing for WaitGetPoses: blocks until the last complete stereo frame
    // has been presented (or the timeout expires), like a VR compositor would.
    void WaitForPresent(unsigned timeoutMs);

    // Short status line (e.g. separation/convergence) drawn at screen depth for
    // a couple of seconds. GPU path only.
    void ShowOsd(const char* text);

    // Live crosstalk-cancellation amount (fraction of the other eye that leaks).
    void SetCrosstalk(float k) { m_crosstalk = k; }

    // Menu detection: the game's menus are head-locked, so the two eyes are a
    // pure horizontal shift of each other; gameplay is not. Returns the current
    // (smoothed) separation in percent to use given the gameplay separation.
    float EffectiveSeparation(float gameplaySeparation);

private:
    Presenter() = default;
    ~Presenter() = default;
    Presenter(const Presenter&) = delete;
    Presenter& operator=(const Presenter&) = delete;

    void ThreadMain();
    bool CreateDeviceAndSwapChain();
    void CreateWindowOnMonitor();
    void PumpMessages();
    void CheckWindowOrder();
    void ManageZOrder();
    HWND FindGameWindow();
    void PresentLatest();
    void PresentGpu(int slot, uint64_t gen, HANDLE handle, const EyeBounds* bounds);
    void Composite(uint8_t* dst, UINT dstPitch);
    void CleanupGraphics();
    void ReleaseCapture();
    void ReleaseGameSlots();
    void ReleasePresenterSlots();
    bool CreateCompositePipeline();
    bool SubmitEyeGpu(int eye, ID3D11Device* dev, ID3D11DeviceContext* ctx, ID3D11Texture2D* src, UINT srcSub,
                      const D3D11_TEXTURE2D_DESC& desc, const EyeBounds& bounds);
    void UpdateOsdTexture(float& alpha);
    void DumpBackBuffer();
    void RunMenuAnalysis();
    void AnalyzeEyes(const uint8_t* data, UINT pitch);
    HRESULT AcquireBackBuffer(ID3D11Texture2D** out);
    HRESULT PresentFrame();
    void SaveDump(const uint8_t* data, int w, int h);
    void MarkPresented(uint64_t serial);

    struct Capture {
        ID3D11Texture2D* staging = nullptr;
        ID3D11Texture2D* resolve = nullptr;
        UINT width = 0, height = 0;
        DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
        UINT mipLevels = 1;
    };

    // GPU path: triple-buffered shared textures (keyed mutex), each holding
    // both eyes side by side. Game side writes, presenter side samples.
    static const int kSlots = 3;
    struct GameSlot {
        ID3D11Texture2D* tex = nullptr;
        IDXGIKeyedMutex* mutex = nullptr;
        HANDLE handle = nullptr;
    };
    struct PresSlot {
        ID3D11Texture2D* tex = nullptr;
        IDXGIKeyedMutex* mutex = nullptr;
        ID3D11ShaderResourceView* srv = nullptr;
        HANDLE handle = nullptr;
    };

    StereoConfig m_cfg{};
    bool m_configured = false;
    std::atomic<bool> m_started{false};
    std::atomic<bool> m_exit{false};

    std::thread m_thread;
    HANDLE m_frameEvent = nullptr;

    std::mutex m_mtx;
    std::vector<uint8_t> m_eye[2];
    int m_eyeW = 0, m_eyeH = 0;
    EyeBounds m_bounds[2];
    bool m_have[2] = {false, false};
    uint64_t m_serial = 0;
    uint64_t m_lastSerial = 0;
    LUID m_luid{};
    bool m_haveLuid = false;

    // Local copy used by presenter thread
    std::vector<uint8_t> m_localEye[2];
    int m_localEyeW = 0, m_localEyeH = 0;
    EyeBounds m_localBounds[2];

    Capture m_capture[2];

    // Frame pacing (guarded by m_mtx)
    std::condition_variable m_presentCv;
    uint64_t m_presentedSerial = 0;
    bool m_deviceReady = false;

    // GPU path, game side (game render thread; indices guarded by m_mtx)
    std::atomic<bool> m_gpuDisabled{false};
    std::atomic<float> m_crosstalk{0.0f};
    GameSlot m_gameSlot[kSlots];
    ID3D11Device* m_slotDevice = nullptr;
    UINT m_slotEyeW = 0, m_slotEyeH = 0;
    DXGI_FORMAT m_slotFormat = DXGI_FORMAT_UNKNOWN;
    uint64_t m_slotGen = 0;
    int m_writeSlot = -1;
    int m_writeMask = 0;
    EyeBounds m_writeBounds[2];
    int m_readySlot = -1;
    int m_readingSlot = -1;
    EyeBounds m_slotBounds[kSlots][2];
    HANDLE m_slotHandle[kSlots] = {};
    DXGI_FORMAT m_slotSrvFormat = DXGI_FORMAT_UNKNOWN;
    bool m_frameIsGpu = false;

    // GPU path, presenter side
    PresSlot m_presSlot[kSlots];
    uint64_t m_presGen = 0;
    ID3D11VertexShader* m_vs = nullptr;
    ID3D11PixelShader* m_ps = nullptr;
    ID3D11Buffer* m_cb = nullptr;
    ID3D11SamplerState* m_sampler = nullptr;
    ID3D11RenderTargetView* m_rtv = nullptr;
    ID3D11Texture2D* m_offscreen = nullptr;   // headless back buffer

    // Menu detection (presenter thread) and resulting separation blend
    ID3D11PixelShader* m_psAnalyze = nullptr;
    ID3D11Texture2D* m_anaTex = nullptr;
    ID3D11RenderTargetView* m_anaRtv = nullptr;
    ID3D11Texture2D* m_anaStaging[3] = {};
    int m_anaWrite = 0, m_anaPending = 0;
    unsigned m_anaFrame = 0;
    float m_residualAvg = 0.03f;          // smoothed pure-shift residual (starts in menus)
    std::atomic<bool> m_inMenu{true};
    std::atomic<float> m_menuBlend{1.0f};    // 1 = menu separation, 0 = gameplay
    LARGE_INTEGER m_lastBlendTime{};
    LARGE_INTEGER m_nextFrame{};

    // OSD: text set by the game thread (guarded by m_mtx), texture on presenter side
    std::string m_osdText;
    uint64_t m_osdVersion = 0;
    unsigned long long m_osdTick = 0;
    uint64_t m_osdBuiltVersion = 0;
    ID3D11Texture2D* m_osdTex = nullptr;
    ID3D11ShaderResourceView* m_osdSrv = nullptr;

    HWND m_hwnd = nullptr;
    LeiaSrHost m_leia;                   // Format=LeiaSR weaver and its textures
    HWND m_hostHwnd = nullptr;           // the game's window our 3D view sits inside (null: separate window)
    DWORD m_launcherPid = 0;             // Rez-infinite.exe, which owns the game's mono mirror window
    std::atomic<HWND> m_launcherHwnd{nullptr};
    bool m_onTop = false;
    RECT m_monitorRect{};
    int m_outW = 0, m_outH = 0;
    wchar_t m_deviceName[32] = {};

    ID3D11Device* m_device = nullptr;
    ID3D11DeviceContext* m_context = nullptr;
    IDXGISwapChain* m_swapChain = nullptr;
    ID3D11Texture2D* m_dynamicTex = nullptr;
    std::vector<uint8_t> m_cpuImage;
    int m_dumpRemaining = 0;
    int m_dumpAfter = 0;
    int m_dumpEvery = 1;
    unsigned long long m_framesSeen = 0;
    unsigned m_dumpIndex = 0;
    UINT m_dynamicW = 0, m_dynamicH = 0;

    int m_format = 0;  // parsed enum
};
