#include "presenter.h"
#include "config.h"
#include "log.h"

#include <d3d11.h>
#include <d3dcompiler.h>
#include <shellapi.h>
#include <dxgi1_2.h>
#include <ppl.h>
#include <tlhelp32.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <string>

// Keep in sync with the pixel shader below.
enum StereoFormat {
    FMT_SBS = 0,
    FMT_SBS_CROSS,
    FMT_TAB,
    FMT_TAB_CROSS,
    FMT_INTERLACED,
    FMT_INTERLACED_REVERSED,
    FMT_CHECKERBOARD,
    FMT_MONO_LEFT,
    FMT_MONO_RIGHT,
    FMT_LEIA_SR  // full-resolution side-by-side pair, woven by the Leia SR runtime
};

static const char* kCompositeHlsl = R"(
cbuffer Params : register(b0) {
    float4 rect0;      // eye 0 uv min.xy / max.zw in the slot texture
    float4 rect1;      // eye 1
    float4 clamp0;     // eye 0 sampling clamp (half texel inside its region)
    float4 clamp1;
    float4 osdRect;    // OSD box in eye uv (min.xy / max.zw)
    float4 osdParams;  // x = opacity
    float4 filt;       // xy = tent half-width in eye uv, zw = tap counts
    float4 color;      // x = crosstalk, y = black lift, z = brightness, w = gamma
    int format; int outW; int outH; int pad;
};
Texture2D tex : register(t0);
Texture2D osd : register(t1);
SamplerState smp : register(s0);

float4 VSMain(uint id : SV_VertexID) : SV_Position {
    float2 p = float2((id << 1) & 2, id & 2);
    return float4(p * float2(2, -2) + float2(-1, 1), 0, 1);
}

float3 Fetch(int e, float2 uv) {
    float4 r = e == 0 ? rect0 : rect1;
    float4 c = e == 0 ? clamp0 : clamp1;
    float2 t = clamp(lerp(r.xy, r.zw, uv), c.xy, c.zw);
    return tex.SampleLevel(smp, t, 0).rgb;
}

// Tent filter over the area one displayed pixel stands for. On an interlaced
// panel each eye only gets every other row, so the vertical footprint is two
// rows: thin horizontal lines fade evenly instead of vanishing or flickering.
// With RenderScale > 1 it is also the supersampling downfilter.
float3 Filtered(int e, float2 uv) {
    int nx = (int)filt.z, ny = (int)filt.w;
    if (nx <= 1 && ny <= 1) return Fetch(e, uv);
    float3 acc = 0;
    float wsum = 0;
    [loop] for (int j = 0; j < ny; j++) {
        float ty = ny > 1 ? ((j + 0.5) / ny) * 2 - 1 : 0;
        [loop] for (int i = 0; i < nx; i++) {
            float tx = nx > 1 ? ((i + 0.5) / nx) * 2 - 1 : 0;
            float w = (1 - abs(tx)) * (1 - abs(ty));
            acc += w * Fetch(e, uv + float2(tx * filt.x, ty * filt.y));
            wsum += w;
        }
    }
    return acc / wsum;
}

float3 Shade(int e, float2 uv) {
    float3 c = Filtered(e, uv);
    float k = color.x;
    if (k > 0 || color.z != 1 || color.w != 1) {
        float3 lin = pow(saturate(c), 2.2);
        if (k > 0) {
            // The panel shows mine + k * other (linear light). Pre-compensate
            // so that adds up to the intended image; black can only be cleaned
            // as far as it is lifted, so `lift` trades black level for ghosting.
            float3 other = pow(saturate(Filtered(1 - e, uv)), 2.2);
            float L = k * color.y;
            float3 dMine = L + (1 - L) * lin;
            float3 dOther = L + (1 - L) * other;
            lin = saturate((dMine - k * dOther) / (1 - k * k));
        }
        lin *= color.z;
        c = pow(saturate(lin), 1.0 / (2.2 * color.w));
    }
    // Same uv in both eyes, so the OSD sits exactly on the screen plane
    float a = osdParams.x;
    if (a > 0) {
        float2 q = (uv - osdRect.xy) / (osdRect.zw - osdRect.xy);
        if (all(q >= 0) && all(q <= 1)) {
            float m = osd.SampleLevel(smp, q, 0).g;
            c *= 1 - 0.6 * a;
            c = lerp(c, float3(1.0, 0.6, 0.2), m * a);
        }
    }
    return c;
}

// Menu detection input: both eyes shrunk to 160x45 luminance, side by side.
float4 PSAnalyze(float4 pos : SV_Position) : SV_Target {
    int e = pos.x < 160 ? 0 : 1;
    float2 cell = float2(1.0 / 160, 1.0 / 45);
    float2 base = float2(pos.x - e * 160 - 0.5, pos.y - 0.5) * cell;
    float lum = 0;
    [unroll] for (int j = 0; j < 4; j++)
        [unroll] for (int i = 0; i < 4; i++)
            lum += dot(Fetch(e, base + (float2(i, j) + 0.5) * 0.25 * cell), float3(0.299, 0.587, 0.114));
    return float4(lum / 16, 0, 0, 1);
}

float4 PSMain(float4 pos : SV_Position) : SV_Target {
    int x = (int)pos.x, y = (int)pos.y;
    float2 uv = pos.xy / float2(outW, outH);
    float3 c;
    if (format == 0 || format == 1) {
        int half = outW / 2;
        int e = x < half ? 0 : 1;
        uv.x = e == 0 ? pos.x / half : (pos.x - half) / (outW - half);
        c = Shade(format == 1 ? 1 - e : e, uv);
    } else if (format == 2 || format == 3) {
        int half = outH / 2;
        int e = y < half ? 0 : 1;
        uv.y = e == 0 ? pos.y / half : (pos.y - half) / (outH - half);
        c = Shade(format == 3 ? 1 - e : e, uv);
    } else if (format == 4 || format == 5) {
        int e = y & 1;
        c = Shade(format == 5 ? 1 - e : e, uv);
    } else if (format == 6) {
        c = Shade((x ^ y) & 1, uv);
    } else {
        c = Shade(format == 8 ? 1 : 0, uv);
    }
    return float4(c, 1);
}
)";

struct CompositeParams {
    float rect[2][4];
    float clampRect[2][4];
    float osdRect[4];
    float osdParams[4];
    float filt[4];
    float color[4];
    int format, outW, outH, pad;
};

// Shareable typeless family for a submitted texture format, and the UNORM view
// used to sample it (raw values, matching what the game wrote).
static DXGI_FORMAT TypelessFormat(DXGI_FORMAT f) {
    switch (f) {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        return DXGI_FORMAT_R8G8B8A8_TYPELESS;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        return DXGI_FORMAT_B8G8R8A8_TYPELESS;
    case DXGI_FORMAT_B8G8R8X8_TYPELESS:
    case DXGI_FORMAT_B8G8R8X8_UNORM:
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
        return DXGI_FORMAT_B8G8R8X8_TYPELESS;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS:
    case DXGI_FORMAT_R10G10B10A2_UNORM:
        return DXGI_FORMAT_R10G10B10A2_TYPELESS;
    default:
        return DXGI_FORMAT_UNKNOWN;
    }
}

static DXGI_FORMAT SrvFormat(DXGI_FORMAT typeless) {
    switch (typeless) {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS: return DXGI_FORMAT_B8G8R8A8_UNORM;
    case DXGI_FORMAT_B8G8R8X8_TYPELESS: return DXGI_FORMAT_B8G8R8X8_UNORM;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS: return DXGI_FORMAT_R10G10B10A2_UNORM;
    default: return DXGI_FORMAT_UNKNOWN;
    }
}

// ResolveSubresource needs a typed format.
static DXGI_FORMAT ResolveFormat(DXGI_FORMAT f) {
    switch (f) {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS: return DXGI_FORMAT_B8G8R8A8_UNORM;
    case DXGI_FORMAT_B8G8R8X8_TYPELESS: return DXGI_FORMAT_B8G8R8X8_UNORM;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS: return DXGI_FORMAT_R10G10B10A2_UNORM;
    default: return f;
    }
}

static const int kOsdW = 640;
static const int kOsdH = 64;

template <typename T> static void SafeRelease(T*& p) {
    if (p) {
        p->Release();
        p = nullptr;
    }
}

static int ParseFormat(const std::string& s) {
    std::string v;
    for (char c : s) v += (char)tolower((unsigned char)c);
    if (v == "sbs" || v == "side-by-side" || v == "sidebyside") return FMT_SBS;
    if (v == "sbs_cross" || v == "sbs-cross" || v == "crossview" || v == "cross") return FMT_SBS_CROSS;
    if (v == "tab" || v == "top-bottom" || v == "topandbottom" || v == "ou") return FMT_TAB;
    if (v == "tab_cross" || v == "tab-cross" || v == "tabcross") return FMT_TAB_CROSS;
    if (v == "interlaced" || v == "interlace" || v == "row-interlaced") return FMT_INTERLACED;
    if (v == "interlaced_reversed" || v == "interlaced-reversed" || v == "interlaced_flip")
        return FMT_INTERLACED_REVERSED;
    if (v == "checkerboard" || v == "checker") return FMT_CHECKERBOARD;
    if (v == "mono" || v == "mono_left" || v == "mono-left") return FMT_MONO_LEFT;
    if (v == "mono_right" || v == "mono-right") return FMT_MONO_RIGHT;
    if (v == "leiasr" || v == "leia_sr" || v == "leia-sr" || v == "leia" || v == "sr") return FMT_LEIA_SR;
    return FMT_SBS;
}

struct MonitorEnumData {
    RECT rect{};
    int nativeW = 0;
    int nativeH = 0;
    wchar_t device[32] = {};
    int index = 0;
    int wanted = 0;
    bool found = false;
};

static BOOL CALLBACK MonitorEnumProc(HMONITOR hMon, HDC, LPRECT, LPARAM lp) {
    auto* data = reinterpret_cast<MonitorEnumData*>(lp);
    if (data->index == data->wanted) {
        MONITORINFOEXW mi{};
        mi.cbSize = sizeof(mi);
        if (GetMonitorInfoW(hMon, &mi)) {
            data->rect = mi.rcMonitor;
            wcsncpy_s(data->device, mi.szDevice, _TRUNCATE);
            DISPLAY_DEVICEW dd{};
            dd.cb = sizeof(dd);
            if (EnumDisplayDevicesW(mi.szDevice, 0, &dd, 0)) {
                DEVMODEW dm{};
                dm.dmSize = sizeof(dm);
                if (EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm)) {
                    data->nativeW = (int)dm.dmPelsWidth;
                    data->nativeH = (int)dm.dmPelsHeight;
                }
            }
            if (data->nativeW == 0) {
                data->nativeW = mi.rcMonitor.right - mi.rcMonitor.left;
                data->nativeH = mi.rcMonitor.bottom - mi.rcMonitor.top;
            }
            data->found = true;
        }
        return FALSE;
    }
    data->index++;
    return TRUE;
}

bool GetMonitorInfoByIndex(int index, RECT& rect, int& nativeW, int& nativeH) {
    MonitorEnumData data;
    data.wanted = index;
    EnumDisplayMonitors(nullptr, nullptr, MonitorEnumProc, (LPARAM)&data);
    if (!data.found) return false;
    rect = data.rect;
    nativeW = data.nativeW;
    nativeH = data.nativeH;
    return true;
}

static bool GetMonitorDeviceName(int index, wchar_t* name, size_t count) {
    MonitorEnumData data;
    data.wanted = index;
    EnumDisplayMonitors(nullptr, nullptr, MonitorEnumProc, (LPARAM)&data);
    if (!data.found) return false;
    wcsncpy_s(name, count, data.device, _TRUNCATE);
    return true;
}

int GetMonitorWidthMM(const StereoConfig& cfg) {
    wchar_t device[32] = {};
    if (!GetMonitorDeviceName(cfg.monitor, device, 32)) return 0;
    DISPLAY_DEVICEW dd{};
    dd.cb = sizeof(dd);
    if (!EnumDisplayDevicesW(device, 0, &dd, EDD_GET_DEVICE_INTERFACE_NAME)) return 0;
    // DeviceID looks like \\?\DISPLAY#GSM5B09#5&2d9f8a8&0&UID4352#{guid}
    std::wstring id = dd.DeviceID;
    size_t p1 = id.find(L'#');
    size_t p2 = p1 == std::wstring::npos ? p1 : id.find(L'#', p1 + 1);
    size_t p3 = p2 == std::wstring::npos ? p2 : id.find(L'#', p2 + 1);
    if (p3 == std::wstring::npos) return 0;
    std::wstring key = L"SYSTEM\\CurrentControlSet\\Enum\\DISPLAY\\" + id.substr(p1 + 1, p2 - p1 - 1) + L"\\" +
                       id.substr(p2 + 1, p3 - p2 - 1) + L"\\Device Parameters";
    uint8_t edid[256] = {};
    DWORD size = sizeof(edid);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, key.c_str(), L"EDID", RRF_RT_REG_BINARY, nullptr, edid, &size) !=
            ERROR_SUCCESS ||
        size < 128)
        return 0;
    // Preferred detailed timing descriptor has the size in mm; byte 21 is cm.
    int mm = edid[66] | ((edid[68] & 0xF0) << 4);
    if (mm <= 0) mm = edid[21] * 10;
    return mm;
}

int GetMonitorRefreshHz(const StereoConfig& cfg) {
    wchar_t device[32] = {};
    if (!GetMonitorDeviceName(cfg.monitor, device, 32)) return 0;
    DEVMODEW dm{};
    dm.dmSize = sizeof(dm);
    if (!EnumDisplaySettingsW(device, ENUM_REGISTRY_SETTINGS, &dm) &&
        !EnumDisplaySettingsW(device, ENUM_CURRENT_SETTINGS, &dm))
        return 0;
    // 0 and 1 mean "hardware default"
    return dm.dmDisplayFrequency > 1 ? (int)dm.dmDisplayFrequency : 0;
}

void ComputeEyeSize(const StereoConfig& cfg, int outW, int outH, int& eyeW, int& eyeH) {
    int fmt = ParseFormat(cfg.format);
    int w = outW, h = outH;
    if (fmt == FMT_SBS || fmt == FMT_SBS_CROSS)
        w = outW / 2;
    else if (fmt == FMT_TAB || fmt == FMT_TAB_CROSS)
        h = outH / 2;
    float scale = cfg.renderScale > 0.05f ? cfg.renderScale : 0.05f;
    if (scale > 2.0f) scale = 2.0f;
    eyeW = (int)(w * scale + 0.5f);
    eyeH = (int)(h * scale + 0.5f);
    if (eyeW < 64) eyeW = 64;
    if (eyeH < 64) eyeH = 64;
    eyeW &= ~1;
    eyeH &= ~1;
}

void GetOutputSize(const StereoConfig& cfg, int& w, int& h, RECT& rect) {
    int nw = 1920, nh = 1080;
    RECT r{ 0, 0, 1920, 1080 };
    GetMonitorInfoByIndex(cfg.monitor, r, nw, nh);

    // Prefer the user's normal desktop mode over the resolution the game may
    // have switched to (Rez sets its flat window mode before VR init).
    wchar_t device[32] = {};
    if (GetMonitorDeviceName(cfg.monitor, device, 32)) {
        DEVMODEW dm{};
        dm.dmSize = sizeof(dm);
        if (EnumDisplaySettingsW(device, ENUM_REGISTRY_SETTINGS, &dm) && dm.dmPelsWidth && dm.dmPelsHeight) {
            nw = (int)dm.dmPelsWidth;
            nh = (int)dm.dmPelsHeight;
        }
    }
    if (cfg.fullscreen) {
        w = nw;
        h = nh;
    } else {
        w = nw > 1280 ? 1280 : nw;
        h = nw > 1280 ? (int)(1280.0 * nh / nw) : nh;
        if (h <= 0) h = 720;
    }
    rect = r;
}

static bool ConvertToBgra(const D3D11_MAPPED_SUBRESOURCE& mapped, UINT w, UINT h, DXGI_FORMAT fmt,
                          std::vector<uint8_t>& out) {
    out.resize((size_t)w * h * 4);
    const uint8_t* src = reinterpret_cast<const uint8_t*>(mapped.pData);
    const size_t dstPitch = (size_t)w * 4;
    bool bgra = false, rgba = false, rgb10a2 = false;

    switch (fmt) {
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8X8_UNORM:
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
        bgra = true;
        break;
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        rgba = true;
        break;
    case DXGI_FORMAT_R10G10B10A2_UNORM:
        rgb10a2 = true;
        break;
    default:
        return false;
    }

    if (bgra) {
        for (UINT y = 0; y < h; y++) {
            memcpy(out.data() + y * dstPitch, src + (size_t)y * mapped.RowPitch, dstPitch);
        }
        return true;
    }

    for (UINT y = 0; y < h; y++) {
        const uint8_t* srow = src + (size_t)y * mapped.RowPitch;
        uint8_t* drow = out.data() + y * dstPitch;
        if (rgba) {
            for (UINT x = 0; x < w; x++) {
                drow[x * 4 + 0] = srow[x * 4 + 2];
                drow[x * 4 + 1] = srow[x * 4 + 1];
                drow[x * 4 + 2] = srow[x * 4 + 0];
                drow[x * 4 + 3] = 255;
            }
        } else if (rgb10a2) {
            const uint32_t* s32 = reinterpret_cast<const uint32_t*>(srow);
            for (UINT x = 0; x < w; x++) {
                uint32_t v = s32[x];
                uint32_t r = (v >> 0) & 0x3FF;
                uint32_t g = (v >> 10) & 0x3FF;
                uint32_t b = (v >> 20) & 0x3FF;
                drow[x * 4 + 0] = (uint8_t)((b * 255) / 1023);
                drow[x * 4 + 1] = (uint8_t)((g * 255) / 1023);
                drow[x * 4 + 2] = (uint8_t)((r * 255) / 1023);
                drow[x * 4 + 3] = 255;
            }
        }
    }
    return true;
}

static LRESULT CALLBACK StereoWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CLOSE:
        // Alt+F4 / taskbar close. Rez in VR mode has no window of its own to
        // shut down through, so end the process (the display mode reverts).
        LogPrintf("Presenter: window closed, exiting the game");
        TerminateProcess(GetCurrentProcess(), 0);
        return 0;
    case WM_DESTROY:
        return 0;
    case WM_ACTIVATE:
        // Ours is only a display. Hand keyboard focus to the game's own window
        // (Alt+Tab or a click on ours); it stays above that window regardless.
        if (LOWORD(wp) != WA_INACTIVE) {
            HWND game = Presenter::Get().LauncherWindow();
            if (game && IsWindow(game)) SetForegroundWindow(game);
        }
        break;
    case WM_ERASEBKGND:
        return 1;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

Presenter& Presenter::Get() {
    static Presenter instance;
    return instance;
}

void Presenter::Configure(const StereoConfig& cfg) {
    m_cfg = cfg;
    m_format = ParseFormat(cfg.format);
    GetOutputSize(cfg, m_outW, m_outH, m_monitorRect);
    wchar_t device[32] = {};
    if (GetMonitorDeviceName(cfg.monitor, device, 32)) wcsncpy_s(m_deviceName, device, _TRUNCATE);
    m_dumpRemaining = cfg.dumpFrames;
    m_dumpAfter = cfg.dumpAfter;
    m_dumpEvery = cfg.dumpEvery > 0 ? cfg.dumpEvery : 1;
    if (!cfg.gpuComposite) m_gpuDisabled = true;
    m_crosstalk = cfg.crosstalk;
    m_configured = true;
    LogPrintf("Presenter: format=%d output=%dx%d monitor=%d fullscreen=%d vsync=%d",
              m_format, m_outW, m_outH, cfg.monitor, cfg.fullscreen, cfg.vsync);
}

void Presenter::Start() {
    if (m_started.exchange(true)) return;
    if (!m_configured) {
        StereoConfig def;
        Configure(def);
    }
    m_exit = false;
    m_serial = 0;
    m_lastSerial = 0;
    m_have[0] = m_have[1] = false;
    if (!m_frameEvent) m_frameEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    m_thread = std::thread(&Presenter::ThreadMain, this);
    LogPrintf("Presenter: thread started");
}

void Presenter::StopForProcessExit() {
    m_exit = true;
    if (m_frameEvent) SetEvent(m_frameEvent);
}

void Presenter::Stop() {
    m_exit = true;
    if (m_frameEvent) SetEvent(m_frameEvent);
    if (m_thread.joinable()) m_thread.join();
    ReleaseCapture();
    ReleaseGameSlots();
    m_started = false;
    LogPrintf("Presenter: stopped");
}

void Presenter::ReleaseCapture() {
    for (auto& cap : m_capture) {
        if (cap.staging) { cap.staging->Release(); cap.staging = nullptr; }
        if (cap.resolve) { cap.resolve->Release(); cap.resolve = nullptr; }
        cap.width = cap.height = 0;
        cap.format = DXGI_FORMAT_UNKNOWN;
    }
}

void Presenter::CreateWindowOnMonitor() {
    static bool classRegistered = false;

    if (m_cfg.headless) {
        LogPrintf("Presenter: headless (off-screen, no window)");
        return;
    }

    if (m_cfg.fullscreen && m_deviceName[0]) {
        DEVMODEW dm{};
        dm.dmSize = sizeof(dm);
        dm.dmPelsWidth = m_outW;
        dm.dmPelsHeight = m_outH;
        dm.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT;
        LONG r = ChangeDisplaySettingsExW(m_deviceName, &dm, nullptr, CDS_FULLSCREEN, nullptr);
        if (r != DISP_CHANGE_SUCCESSFUL) {
            LogPrintf("Presenter: ChangeDisplaySettingsEx %ls %dx%d failed (%ld)", m_deviceName, m_outW, m_outH, r);
        } else {
            LogPrintf("Presenter: display mode set to %dx%d on %ls", m_outW, m_outH, m_deviceName);
        }
    }
    if (!classRegistered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = StereoWndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = nullptr;
        {
            // The game's own icon for the taskbar / Alt+Tab
            wchar_t exe[MAX_PATH] = {};
            GetModuleFileNameW(nullptr, exe, MAX_PATH);
            wc.hIcon = ExtractIconW(GetModuleHandleW(nullptr), exe, 0);
        }
        wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
        wc.lpszClassName = L"Rez3DScreen";
        RegisterClassExW(&wc);
        classRegistered = true;
    }

    int x = m_monitorRect.left + (m_monitorRect.right - m_monitorRect.left - m_outW) / 2;
    int y = m_monitorRect.top + (m_monitorRect.bottom - m_monitorRect.top - m_outH) / 2;
    // Rez-infinite.exe starts REZ.exe and keeps a fullscreen, always-on-top window
    // that shows the game's mono mirror. ManageZOrder keeps ours above it.
    {
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        PROCESSENTRY32W pe{ sizeof(pe) };
        DWORD self = GetCurrentProcessId(), parent = 0;
        if (snap != INVALID_HANDLE_VALUE) {
            for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe))
                if (pe.th32ProcessID == self) parent = pe.th32ParentProcessID;
            for (BOOL ok = Process32FirstW(snap, &pe); ok && parent; ok = Process32NextW(snap, &pe))
                if (pe.th32ProcessID == parent && _wcsicmp(pe.szExeFile, L"Rez-infinite.exe") == 0)
                    m_launcherPid = parent;
            CloseHandle(snap);
        }
        LogPrintf("Presenter: launcher process %lu", m_launcherPid);
    }
    // Preferred: show the 3D output inside the game's own window (REZ.exe's
    // "RezWIN", which the launcher embeds in its fullscreen window) as a child
    // surface covering it. Alt+Tab, focus and Alt+F4 then stay the game's own.
    HWND host = nullptr;
    // Area X's window appears (and is embedded by the launcher) a few seconds after VR init
    for (int i = 0; i < 200 && !host && !m_exit; i++) {
        host = FindGameWindow();
        if (!host) Sleep(100);
    }
    if (host) {
        RECT rc{};
        GetClientRect(host, &rc);
        SetWindowLongW(host, GWL_STYLE, GetWindowLongW(host, GWL_STYLE) | WS_CLIPCHILDREN);
        // Disabled: mouse and keyboard keep going to the game's window
        m_hwnd = CreateWindowExW(0, L"Rez3DScreen", L"Rez Infinite 3D",
                                 WS_CHILD | WS_VISIBLE | WS_DISABLED | WS_CLIPSIBLINGS, 0, 0, rc.right, rc.bottom,
                                 host, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (m_hwnd) {
            m_hostHwnd = host;
            SetWindowPos(m_hwnd, HWND_TOP, 0, 0, rc.right, rc.bottom, SWP_NOACTIVATE | SWP_SHOWWINDOW);
            char cls[64] = {};
            GetClassNameA(host, cls, sizeof(cls));
            LogPrintf("Presenter: 3D view inside the game window '%s' (%ldx%ld)", cls, rc.right, rc.bottom);
            return;
        }
        LogPrintf("Presenter: child window failed (err=%lu), using a separate window", GetLastError());
    } else {
        LogPrintf("Presenter: game window not found, using a separate window");
    }
    // Fallback: a normal app window that shows in Alt+Tab and the taskbar. It
    // is topmost only while the game is the active app (see ManageZOrder).
    m_hwnd = CreateWindowExW(WS_EX_APPWINDOW, L"Rez3DScreen",
                             L"Rez Infinite 3D", WS_POPUP, x, y, m_outW, m_outH, nullptr, nullptr,
                             GetModuleHandleW(nullptr), nullptr);
    if (!m_hwnd) {
        LogPrintf("Presenter: CreateWindowEx failed, err=%lu", GetLastError());
        return;
    }
    ShowWindow(m_hwnd, SW_SHOW);
    SetForegroundWindow(m_hwnd);
    SetWindowPos(m_hwnd, HWND_TOP, x, y, m_outW, m_outH, SWP_SHOWWINDOW);
    LogPrintf("Presenter: window created at %d,%d %dx%d", x, y, m_outW, m_outH);
}

bool Presenter::CreateDeviceAndSwapChain() {
    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&factory))) {
        LogPrintf("Presenter: CreateDXGIFactory1 failed");
        return false;
    }

    IDXGIAdapter1* adapter = nullptr;
    if (m_cfg.adapter >= 0) {
        factory->EnumAdapters1((UINT)m_cfg.adapter, &adapter);
    } else if (m_haveLuid) {
        IDXGIAdapter1* a = nullptr;
        for (UINT i = 0; factory->EnumAdapters1(i, &a) != DXGI_ERROR_NOT_FOUND; i++) {
            DXGI_ADAPTER_DESC1 desc{};
            a->GetDesc1(&desc);
            if (desc.AdapterLuid.LowPart == m_luid.LowPart && desc.AdapterLuid.HighPart == m_luid.HighPart) {
                adapter = a;
                break;
            }
            a->Release();
            a = nullptr;
        }
        if (adapter) {
            DXGI_ADAPTER_DESC1 desc{};
            adapter->GetDesc1(&desc);
            LogPrintf("Presenter: using adapter LUID match: %ls", desc.Description);
        }
    }

    UINT flags = 0;
    D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
                                   D3D_FEATURE_LEVEL_10_0 };
    D3D_FEATURE_LEVEL got{};
    HRESULT hr = D3D11CreateDevice(adapter, adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE,
                                   nullptr, flags, levels, _countof(levels), D3D11_SDK_VERSION,
                                   &m_device, &got, &m_context);
    if (FAILED(hr)) {
        LogPrintf("Presenter: D3D11CreateDevice failed hr=0x%08lX", (unsigned long)hr);
        if (adapter) adapter->Release();
        factory->Release();
        return false;
    }
    if (adapter) {
        DXGI_ADAPTER_DESC1 desc{};
        adapter->GetDesc1(&desc);
        LogPrintf("Presenter: adapter: %ls", desc.Description);
    }

    if (m_cfg.headless) {
        factory->Release();
        if (adapter) adapter->Release();
        D3D11_TEXTURE2D_DESC od{};
        od.Width = m_outW;
        od.Height = m_outH;
        od.MipLevels = 1;
        od.ArraySize = 1;
        od.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        od.SampleDesc.Count = 1;
        od.Usage = D3D11_USAGE_DEFAULT;
        od.BindFlags = D3D11_BIND_RENDER_TARGET;
        hr = m_device->CreateTexture2D(&od, nullptr, &m_offscreen);
        if (FAILED(hr)) {
            LogPrintf("Presenter: off-screen target failed hr=0x%08lX", (unsigned long)hr);
            return false;
        }
    } else {
    // Flip model: lower latency and independent flip for a borderless fullscreen
    // window. Falls back to the legacy blt model on older systems.
    hr = E_FAIL;
    IDXGIFactory2* factory2 = nullptr;
    if (SUCCEEDED(factory->QueryInterface(__uuidof(IDXGIFactory2), (void**)&factory2)) && factory2) {
        DXGI_SWAP_CHAIN_DESC1 sd1{};
        sd1.Width = m_outW;
        sd1.Height = m_outH;
        sd1.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        sd1.SampleDesc.Count = 1;
        sd1.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd1.BufferCount = 2;
        sd1.Scaling = DXGI_SCALING_STRETCH;
        sd1.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        IDXGISwapChain1* sc1 = nullptr;
        hr = factory2->CreateSwapChainForHwnd(m_device, m_hwnd, &sd1, nullptr, nullptr, &sc1);
        if (SUCCEEDED(hr)) m_swapChain = sc1;
        factory2->Release();
    }
    if (FAILED(hr)) {
        DXGI_SWAP_CHAIN_DESC sd{};
        sd.BufferDesc.Width = m_outW;
        sd.BufferDesc.Height = m_outH;
        sd.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        sd.BufferDesc.RefreshRate.Numerator = 0;
        sd.BufferDesc.RefreshRate.Denominator = 1;
        sd.SampleDesc.Count = 1;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.BufferCount = 1;
        sd.OutputWindow = m_hwnd;
        sd.Windowed = TRUE;
        sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        hr = factory->CreateSwapChain(m_device, &sd, &m_swapChain);
        LogPrintf("Presenter: flip model unavailable, using blt swapchain");
    }
    factory->MakeWindowAssociation(m_hwnd, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES);
    factory->Release();
    if (adapter) adapter->Release();
    if (FAILED(hr)) {
        LogPrintf("Presenter: CreateSwapChain failed hr=0x%08lX", (unsigned long)hr);
        return false;
    }

    }

    IDXGIDevice1* dxgiDev1 = nullptr;
    if (SUCCEEDED(m_device->QueryInterface(__uuidof(IDXGIDevice1), (void**)&dxgiDev1)) && dxgiDev1) {
        dxgiDev1->SetMaximumFrameLatency(1);
        dxgiDev1->Release();
    }

    if (!CreateCompositePipeline()) {
        LogPrintf("Presenter: shader pipeline unavailable, GPU compositing disabled");
        m_gpuDisabled = true;
    }

    D3D11_TEXTURE2D_DESC td{};
    td.Width = m_outW;
    td.Height = m_outH;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = 0;
    td.CPUAccessFlags = 0;
    hr = m_device->CreateTexture2D(&td, nullptr, &m_dynamicTex);
    if (FAILED(hr)) {
        LogPrintf("Presenter: CreateTexture2D failed hr=0x%08lX", (unsigned long)hr);
        return false;
    }
    m_dynamicW = m_outW;
    m_dynamicH = m_outH;
    LogPrintf("Presenter: D3D11 device and swapchain ready");
    return true;
}

bool Presenter::CreateCompositePipeline() {
    ID3DBlob* vsBlob = nullptr;
    ID3DBlob* psBlob = nullptr;
    ID3DBlob* err = nullptr;
    HRESULT hr = D3DCompile(kCompositeHlsl, strlen(kCompositeHlsl), "composite", nullptr, nullptr, "VSMain",
                            "vs_4_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vsBlob, &err);
    if (SUCCEEDED(hr)) {
        SafeRelease(err);
        hr = D3DCompile(kCompositeHlsl, strlen(kCompositeHlsl), "composite", nullptr, nullptr, "PSMain", "ps_4_0",
                        D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &psBlob, &err);
    }
    if (FAILED(hr)) {
        LogPrintf("Presenter: shader compile failed hr=0x%08lX %s", (unsigned long)hr,
                  err ? (const char*)err->GetBufferPointer() : "");
        SafeRelease(err);
        SafeRelease(vsBlob);
        SafeRelease(psBlob);
        return false;
    }
    ID3DBlob* anaBlob = nullptr;
    if (SUCCEEDED(D3DCompile(kCompositeHlsl, strlen(kCompositeHlsl), "composite", nullptr, nullptr, "PSAnalyze",
                             "ps_4_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &anaBlob, nullptr))) {
        m_device->CreatePixelShader(anaBlob->GetBufferPointer(), anaBlob->GetBufferSize(), nullptr, &m_psAnalyze);
        SafeRelease(anaBlob);
    }
    if (m_psAnalyze) {
        D3D11_TEXTURE2D_DESC ad{};
        ad.Width = 320;
        ad.Height = 45;
        ad.MipLevels = 1;
        ad.ArraySize = 1;
        ad.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        ad.SampleDesc.Count = 1;
        ad.Usage = D3D11_USAGE_DEFAULT;
        ad.BindFlags = D3D11_BIND_RENDER_TARGET;
        bool ok = SUCCEEDED(m_device->CreateTexture2D(&ad, nullptr, &m_anaTex)) &&
                  SUCCEEDED(m_device->CreateRenderTargetView(m_anaTex, nullptr, &m_anaRtv));
        ad.Usage = D3D11_USAGE_STAGING;
        ad.BindFlags = 0;
        ad.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        for (auto& st : m_anaStaging) ok = ok && SUCCEEDED(m_device->CreateTexture2D(&ad, nullptr, &st));
        if (!ok) SafeRelease(m_psAnalyze);
    }
    hr = m_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &m_vs);
    if (SUCCEEDED(hr))
        hr = m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &m_ps);
    SafeRelease(vsBlob);
    SafeRelease(psBlob);
    if (FAILED(hr)) return false;

    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = sizeof(CompositeParams);
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    if (FAILED(m_device->CreateBuffer(&bd, nullptr, &m_cb))) return false;

    D3D11_SAMPLER_DESC smp{};
    smp.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    smp.AddressU = smp.AddressV = smp.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    smp.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(m_device->CreateSamplerState(&smp, &m_sampler))) return false;

    ID3D11Texture2D* backBuffer = nullptr;
    if (FAILED(AcquireBackBuffer(&backBuffer))) return false;
    hr = m_device->CreateRenderTargetView(backBuffer, nullptr, &m_rtv);
    backBuffer->Release();
    return SUCCEEDED(hr);
}

void Presenter::SubmitEye(int eye, ID3D11Texture2D* texture, const EyeBounds& bounds, int submitFlags) {
    (void)submitFlags;
    if (!m_started.load()) Start();
    if (!texture) return;
    if (eye != 0) eye = 1;

    ID3D11Device* dev = nullptr;
    texture->GetDevice(&dev);
    if (!dev) return;
    ID3D11DeviceContext* ctx = nullptr;
    dev->GetImmediateContext(&ctx);
    if (!ctx) {
        dev->Release();
        return;
    }

    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);

    UINT subresource = 0;
    if (desc.ArraySize > 1) {
        UINT slice = ((UINT)eye < desc.ArraySize) ? (UINT)eye : 0;
        subresource = D3D11CalcSubresource(0, slice, desc.MipLevels);
    }

    Capture& cap = m_capture[eye];
    if (cap.width != desc.Width || cap.height != desc.Height || cap.format != desc.Format) {
        SafeRelease(cap.staging);
        SafeRelease(cap.resolve);
        cap.width = desc.Width;
        cap.height = desc.Height;
        cap.format = desc.Format;
        cap.mipLevels = desc.MipLevels;
        LogPrintf("SubmitEye: capture %ux%u fmt=%d samples=%u array=%u", desc.Width, desc.Height, (int)desc.Format,
                  desc.SampleDesc.Count, desc.ArraySize);
    }

    // Both paths need a single-sampled source
    ID3D11Texture2D* src = texture;
    UINT srcSub = subresource;
    if (desc.SampleDesc.Count > 1) {
        if (!cap.resolve) {
            D3D11_TEXTURE2D_DESC rd = desc;
            rd.MipLevels = 1;
            rd.ArraySize = 1;
            rd.SampleDesc.Count = 1;
            rd.SampleDesc.Quality = 0;
            rd.Usage = D3D11_USAGE_DEFAULT;
            rd.BindFlags = 0;
            rd.CPUAccessFlags = 0;
            rd.MiscFlags = 0;
            if (FAILED(dev->CreateTexture2D(&rd, nullptr, &cap.resolve))) {
                LogPrintf("SubmitEye: resolve texture creation failed");
                ctx->Release();
                dev->Release();
                return;
            }
        }
        ctx->ResolveSubresource(cap.resolve, 0, texture, subresource, ResolveFormat(desc.Format));
        src = cap.resolve;
        srcSub = 0;
    }

    if (!m_gpuDisabled.load() && SubmitEyeGpu(eye, dev, ctx, src, srcSub, desc, bounds)) {
        ctx->Release();
        dev->Release();
        return;
    }

    // CPU fallback: read back through a staging texture
    if (!cap.staging) {
        D3D11_TEXTURE2D_DESC sd{};
        sd.Width = desc.Width;
        sd.Height = desc.Height;
        sd.MipLevels = 1;
        sd.ArraySize = 1;
        sd.Format = desc.Format;
        sd.SampleDesc.Count = 1;
        sd.Usage = D3D11_USAGE_STAGING;
        sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        HRESULT hr = dev->CreateTexture2D(&sd, nullptr, &cap.staging);
        if (FAILED(hr)) {
            LogPrintf("SubmitEye: staging texture creation failed hr=0x%08lX fmt=%d %ux%u",
                      (unsigned long)hr, (int)desc.Format, desc.Width, desc.Height);
            ctx->Release();
            dev->Release();
            return;
        }
        LogPrintf("SubmitEye: using CPU readback path");
    }
    ctx->CopySubresourceRegion(cap.staging, 0, 0, 0, 0, src, srcSub, nullptr);

    D3D11_MAPPED_SUBRESOURCE mapped{};
    HRESULT hr = ctx->Map(cap.staging, 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        LogPrintf("SubmitEye: Map failed hr=0x%08lX", (unsigned long)hr);
        ctx->Release();
        dev->Release();
        return;
    }

    static bool warnedFormat = false;
    std::vector<uint8_t> converted;
    bool ok = ConvertToBgra(mapped, desc.Width, desc.Height, ResolveFormat(desc.Format), converted);
    ctx->Unmap(cap.staging, 0);

    if (!ok) {
        if (!warnedFormat) {
            LogPrintf("SubmitEye: unsupported texture format %d", (int)desc.Format);
            warnedFormat = true;
        }
        ctx->Release();
        dev->Release();
        return;
    }

    static int statCount = 0;
    int statIndex = statCount++;
    if (statIndex < 6) {
        uint64_t sum[3] = {};
        uint8_t mn[3] = {255, 255, 255};
        uint8_t mx[3] = {0, 0, 0};
        size_t samples = 0;
        size_t pixels = (size_t)desc.Width * desc.Height;
        for (size_t p = 0; p < pixels; p += 97) {
            const uint8_t* q = converted.data() + p * 4;
            for (int c = 0; c < 3; c++) {
                uint8_t v = q[c];
                sum[c] += v;
                if (v < mn[c]) mn[c] = v;
                if (v > mx[c]) mx[c] = v;
            }
            samples++;
        }
        LogPrintf("SubmitEye stats #%d eye=%d %ux%u min=%u/%u/%u max=%u/%u/%u avg=%u/%u/%u", statIndex,
                  eye, desc.Width, desc.Height, mn[0], mn[1], mn[2], mx[0], mx[1], mx[2],
                  (unsigned)(sum[0] / (samples ? samples : 1)), (unsigned)(sum[1] / (samples ? samples : 1)),
                  (unsigned)(sum[2] / (samples ? samples : 1)));
    }

    {
        std::lock_guard<std::mutex> lk(m_mtx);
        if (!m_haveLuid) {
            IDXGIDevice* dxgiDev = nullptr;
            if (SUCCEEDED(dev->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDev)) && dxgiDev) {
                IDXGIAdapter* adapter = nullptr;
                if (SUCCEEDED(dxgiDev->GetAdapter(&adapter)) && adapter) {
                    DXGI_ADAPTER_DESC ad{};
                    if (SUCCEEDED(adapter->GetDesc(&ad))) {
                        m_luid = ad.AdapterLuid;
                        m_haveLuid = true;
                    }
                    adapter->Release();
                }
                dxgiDev->Release();
            }
        }

        m_eyeW = (int)desc.Width;
        m_eyeH = (int)desc.Height;
        m_eye[eye].swap(converted);
        m_bounds[eye] = bounds;
        m_have[eye] = true;
        if (m_have[0] && m_have[1]) {
            m_serial++;
            m_frameIsGpu = false;
            m_have[0] = m_have[1] = false;
            SetEvent(m_frameEvent);
        }
    }

    ctx->Release();
    dev->Release();
}

void Presenter::ReleaseGameSlots() {
    for (auto& s : m_gameSlot) {
        SafeRelease(s.mutex);
        SafeRelease(s.tex);
        s.handle = nullptr;
    }
    m_slotDevice = nullptr;
    m_writeSlot = -1;
    m_writeMask = 0;
}

bool Presenter::SubmitEyeGpu(int eye, ID3D11Device* dev, ID3D11DeviceContext* ctx, ID3D11Texture2D* src,
                             UINT srcSub, const D3D11_TEXTURE2D_DESC& desc, const EyeBounds& bounds) {
    const DXGI_FORMAT family = TypelessFormat(desc.Format);
    if (family == DXGI_FORMAT_UNKNOWN) {
        LogPrintf("SubmitEye: format %d not shareable, using CPU path", (int)desc.Format);
        m_gpuDisabled = true;
        return false;
    }

    if (m_slotDevice != dev || m_slotEyeW != desc.Width || m_slotEyeH != desc.Height || m_slotFormat != family) {
        ReleaseGameSlots();
        D3D11_TEXTURE2D_DESC td{};
        td.Width = desc.Width * 2;
        td.Height = desc.Height;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = family;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        td.MiscFlags = D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
        for (auto& s : m_gameSlot) {
            HRESULT hr = dev->CreateTexture2D(&td, nullptr, &s.tex);
            IDXGIResource* res = nullptr;
            if (SUCCEEDED(hr)) hr = s.tex->QueryInterface(__uuidof(IDXGIKeyedMutex), (void**)&s.mutex);
            if (SUCCEEDED(hr)) hr = s.tex->QueryInterface(__uuidof(IDXGIResource), (void**)&res);
            if (SUCCEEDED(hr)) hr = res->GetSharedHandle(&s.handle);
            SafeRelease(res);
            if (FAILED(hr)) {
                LogPrintf("SubmitEye: shared texture creation failed hr=0x%08lX, using CPU path", (unsigned long)hr);
                ReleaseGameSlots();
                m_gpuDisabled = true;
                return false;
            }
        }
        m_slotDevice = dev;
        m_slotEyeW = desc.Width;
        m_slotEyeH = desc.Height;
        m_slotFormat = family;
        std::lock_guard<std::mutex> lk(m_mtx);
        m_slotGen++;
        m_readySlot = m_readingSlot = -1;
        for (int i = 0; i < kSlots; i++) m_slotHandle[i] = m_gameSlot[i].handle;
        m_slotSrvFormat = SrvFormat(family);
        if (!m_haveLuid) {
            IDXGIDevice* dxgiDev = nullptr;
            if (SUCCEEDED(dev->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDev)) && dxgiDev) {
                IDXGIAdapter* adapter = nullptr;
                if (SUCCEEDED(dxgiDev->GetAdapter(&adapter)) && adapter) {
                    DXGI_ADAPTER_DESC ad{};
                    if (SUCCEEDED(adapter->GetDesc(&ad))) {
                        m_luid = ad.AdapterLuid;
                        m_haveLuid = true;
                    }
                    adapter->Release();
                }
                dxgiDev->Release();
            }
        }
        LogPrintf("SubmitEye: GPU path, %d shared slots %ux%u fmt=%d", kSlots, td.Width, td.Height, (int)family);
    }

    if (m_writeSlot < 0) {
        std::lock_guard<std::mutex> lk(m_mtx);
        for (int i = 0; i < kSlots; i++) {
            if (i != m_readySlot && i != m_readingSlot) {
                m_writeSlot = i;
                break;
            }
        }
        m_writeMask = 0;
    }
    GameSlot& slot = m_gameSlot[m_writeSlot];

    // Only contended if the presenter is mid-draw on this slot, which the slot
    // selection above avoids; the timeout just guards against a stuck presenter.
    HRESULT hr = slot.mutex->AcquireSync(0, 100);
    if (hr != S_OK) {
        static unsigned s_timeouts = 0;
        if (s_timeouts++ < 5) LogPrintf("SubmitEye: slot %d acquire hr=0x%08lX, frame dropped", m_writeSlot,
                                        (unsigned long)hr);
        return true;
    }
    ctx->CopySubresourceRegion(slot.tex, 0, eye * desc.Width, 0, 0, src, srcSub, nullptr);
    slot.mutex->ReleaseSync(0);

    m_writeBounds[eye] = bounds;
    m_writeMask |= 1 << eye;
    if (m_writeMask == 3) {
        std::lock_guard<std::mutex> lk(m_mtx);
        m_slotBounds[m_writeSlot][0] = m_writeBounds[0];
        m_slotBounds[m_writeSlot][1] = m_writeBounds[1];
        m_readySlot = m_writeSlot;
        m_serial++;
        m_frameIsGpu = true;
        m_have[0] = m_have[1] = false;
        m_writeSlot = -1;
        m_writeMask = 0;
        SetEvent(m_frameEvent);
    }
    return true;
}

void Presenter::WaitForPresent(unsigned timeoutMs) {
    std::unique_lock<std::mutex> lk(m_mtx);
    if (!m_deviceReady || m_exit) return;
    m_presentCv.wait_for(lk, std::chrono::milliseconds(timeoutMs),
                         [&] { return m_presentedSerial >= m_serial || m_exit || !m_deviceReady; });
}

void Presenter::MarkPresented(uint64_t serial) {
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        if (serial > m_presentedSerial) m_presentedSerial = serial;
    }
    m_presentCv.notify_all();
}

// Logs this process's visible top-level windows in z-order (top first) whenever
// that list changes, so we can tell if the game's own mirror window covers ours.
void Presenter::CheckWindowOrder() {
    static ULONGLONG last = 0;
    ULONGLONG now = GetTickCount64();
    if (now - last < 1000) return;
    last = now;
    struct Ctx { DWORD pid; HWND ours; std::string text; } ctx{ GetCurrentProcessId(), m_hwnd, "" };
    EnumWindows([](HWND h, LPARAM lp) -> BOOL {
        auto* c = reinterpret_cast<Ctx*>(lp);
        DWORD pid = 0;
        GetWindowThreadProcessId(h, &pid);
        if (pid != c->pid || !IsWindowVisible(h)) return TRUE;
        char cls[64] = {}, title[64] = {};
        GetClassNameA(h, cls, sizeof(cls));
        GetWindowTextA(h, title, sizeof(title));
        RECT r{};
        GetWindowRect(h, &r);
        LONG ex = GetWindowLongA(h, GWL_EXSTYLE);
        char line[256];
        sprintf_s(line, "[%s%s '%s' %ld,%ld %ldx%ld%s] ", h == c->ours ? "OURS " : "", cls, title, r.left, r.top,
                  r.right - r.left, r.bottom - r.top, (ex & WS_EX_TOPMOST) ? " topmost" : "");
        c->text += line;
        return TRUE;
    }, (LPARAM)&ctx);
    char fg[64] = {};
    HWND f = GetForegroundWindow();
    DWORD fpid = 0;
    GetWindowThreadProcessId(f, &fpid);
    sprintf_s(fg, " foreground=%s%s", f == m_hwnd ? "ours" : (fpid == ctx.pid ? "game" : (fpid && fpid == m_launcherPid ? "launcher" : "other")),
              m_onTop ? " (3D on top)" : "");
    ctx.text += fg;
    static std::string prev;
    static unsigned logged = 0;
    if (ctx.text != prev && logged < 50) {
        logged++;
        LogPrintf("Windows (top first): %s", ctx.text.c_str());
    }
    prev = ctx.text;
}

// The game's own visible window: REZ.exe's RezWIN or Area X's UnrealWindow,
// embedded in the launcher's window, or a top-level window of this process when
// started without it. Area X is started by REZ.exe, which then exits, so the
// launcher isn't always our parent: any top-level window hosting one of ours counts.
HWND Presenter::FindGameWindow() {
    struct Ctx { DWORD self, launcher; HWND best; LONG area; } ctx{ GetCurrentProcessId(), m_launcherPid, nullptr, 0 };
    EnumWindows([](HWND h, LPARAM lp) -> BOOL {
        auto* c = reinterpret_cast<Ctx*>(lp);
        DWORD pid = 0;
        GetWindowThreadProcessId(h, &pid);
        auto consider = [](HWND k, LPARAM l) -> BOOL {
            auto* c = reinterpret_cast<Ctx*>(l);
            DWORD p = 0;
            GetWindowThreadProcessId(k, &p);
            if (p != c->self || !IsWindowVisible(k)) return TRUE;
            char cls[64] = {};
            GetClassNameA(k, cls, sizeof(cls));
            if (!strcmp(cls, "Rez3DScreen") || !strcmp(cls, "IME") || !strcmp(cls, "MSCTFIME UI")) return TRUE;
            RECT r{};
            GetClientRect(k, &r);
            LONG area = r.right * r.bottom;
            if (area > c->area) { c->area = area; c->best = k; }
            return TRUE;
        };
        if (pid == c->self) consider(h, lp);
        else EnumChildWindows(h, consider, lp);
        return TRUE;
    }, (LPARAM)&ctx);
    return ctx.area >= 640 * 360 ? ctx.best : nullptr;
}

// While the game (REZ.exe, its launcher window or ours) is the active app, our
// window is topmost and the launcher's mirror window is demoted, so the mirror
// can never cover the 3D output. When the user switches to another app we drop
// below it, so Alt+Tab works and nothing of the game stays on top.
void Presenter::ManageZOrder() {
    if (!m_hwnd) return;
    if (m_hostHwnd) {
        // Inside the game's window: just follow its size
        RECT host{}, ours{};
        GetClientRect(m_hostHwnd, &host);
        GetClientRect(m_hwnd, &ours);
        if (host.right != ours.right || host.bottom != ours.bottom)
            SetWindowPos(m_hwnd, HWND_TOP, 0, 0, host.right, host.bottom, SWP_NOACTIVATE);
        return;
    }
    HWND launcher = m_launcherHwnd.load();
    if (m_launcherPid && (!launcher || !IsWindow(launcher))) {
        struct Ctx { DWORD pid; HWND found; } ctx{ m_launcherPid, nullptr };
        EnumWindows([](HWND h, LPARAM lp) -> BOOL {
            auto* c = reinterpret_cast<Ctx*>(lp);
            DWORD pid = 0;
            GetWindowThreadProcessId(h, &pid);
            if (pid != c->pid || !IsWindowVisible(h) || GetWindow(h, GW_OWNER)) return TRUE;
            c->found = h;
            return FALSE;
        }, (LPARAM)&ctx);
        if (ctx.found != launcher) {
            launcher = ctx.found;
            m_launcherHwnd = launcher;
            if (launcher) LogPrintf("Presenter: launcher window found, keeping 3D above it");
        }
    }
    const UINT flags = SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER;
    if (launcher && (GetWindowLongW(launcher, GWL_EXSTYLE) & WS_EX_TOPMOST))
        SetWindowPos(launcher, HWND_NOTOPMOST, 0, 0, 0, 0, flags);

    HWND fg = GetForegroundWindow();
    DWORD fpid = 0;
    GetWindowThreadProcessId(fg, &fpid);
    bool active = fg == m_hwnd || fpid == GetCurrentProcessId() || (m_launcherPid && fpid == m_launcherPid);
    if (active == m_onTop) return;
    m_onTop = active;
    if (active) {
        SetWindowPos(m_hwnd, HWND_TOPMOST, 0, 0, 0, 0, flags);
    } else {
        SetWindowPos(m_hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, flags);
        if (fg) SetWindowPos(m_hwnd, fg, 0, 0, 0, 0, flags);  // stay behind the app the user switched to
    }
}

void Presenter::PumpMessages() {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

void Presenter::ThreadMain() {
    CreateWindowOnMonitor();
    if (!m_hwnd && !m_cfg.headless) return;

    for (int i = 0; i < 100 && !m_exit; i++) {
        if (m_haveLuid || m_cfg.adapter >= 0) break;
        Sleep(50);
    }
    if (!CreateDeviceAndSwapChain()) return;
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        m_deviceReady = true;
    }

    while (!m_exit) {
        PumpMessages();
        // Wake on window messages too: our view may be a child of the game's window,
        // whose thread then waits on us for some messages
        MsgWaitForMultipleObjects(1, &m_frameEvent, FALSE, 100, QS_ALLINPUT);
        PumpMessages();
        ManageZOrder();
        CheckWindowOrder();

        bool haveCpu = false;
        int gpuSlot = -1;
        uint64_t gen = 0, serial = 0;
        HANDLE handle = nullptr;
        EyeBounds gpuBounds[2];
        {
            std::lock_guard<std::mutex> lk(m_mtx);
            if (m_serial != m_lastSerial) {
                m_lastSerial = m_serial;
                serial = m_serial;
                if (m_frameIsGpu && m_readySlot >= 0) {
                    gpuSlot = m_readySlot;
                    m_readingSlot = gpuSlot;
                    gen = m_slotGen;
                    handle = m_slotHandle[gpuSlot];
                    gpuBounds[0] = m_slotBounds[gpuSlot][0];
                    gpuBounds[1] = m_slotBounds[gpuSlot][1];
                } else if (!m_frameIsGpu && m_eyeW > 0 && m_eyeH > 0) {
                    std::swap(m_localEye[0], m_eye[0]);
                    std::swap(m_localEye[1], m_eye[1]);
                    m_localEyeW = m_eyeW;
                    m_localEyeH = m_eyeH;
                    m_localBounds[0] = m_bounds[0];
                    m_localBounds[1] = m_bounds[1];
                    haveCpu = true;
                }
            }
        }
        if (gpuSlot >= 0) {
            PresentGpu(gpuSlot, gen, handle, gpuBounds);
            std::lock_guard<std::mutex> lk(m_mtx);
            m_readingSlot = -1;
        } else if (haveCpu) {
            PresentLatest();
        }
        if (serial) MarkPresented(serial);
    }

    {
        std::lock_guard<std::mutex> lk(m_mtx);
        m_deviceReady = false;
    }
    m_presentCv.notify_all();
    CleanupGraphics();
}

void Presenter::ReleasePresenterSlots() {
    for (auto& s : m_presSlot) {
        SafeRelease(s.srv);
        SafeRelease(s.mutex);
        SafeRelease(s.tex);
        s.handle = nullptr;
    }
}

void Presenter::PresentGpu(int slot, uint64_t gen, HANDLE handle, const EyeBounds* bounds) {
    if (!m_device || !m_rtv) return;

    if (gen != m_presGen) {
        ReleasePresenterSlots();
        m_presGen = gen;
    }
    PresSlot& ps = m_presSlot[slot];
    if (!ps.tex || ps.handle != handle) {
        SafeRelease(ps.srv);
        SafeRelease(ps.mutex);
        SafeRelease(ps.tex);
        HRESULT hr = m_device->OpenSharedResource(handle, __uuidof(ID3D11Texture2D), (void**)&ps.tex);
        if (SUCCEEDED(hr)) hr = ps.tex->QueryInterface(__uuidof(IDXGIKeyedMutex), (void**)&ps.mutex);
        DXGI_FORMAT srvFmt;
        {
            std::lock_guard<std::mutex> lk(m_mtx);
            srvFmt = m_slotSrvFormat;
        }
        if (SUCCEEDED(hr)) {
            D3D11_SHADER_RESOURCE_VIEW_DESC sv{};
            sv.Format = srvFmt;
            sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            sv.Texture2D.MipLevels = 1;
            hr = m_device->CreateShaderResourceView(ps.tex, &sv, &ps.srv);
        }
        if (FAILED(hr)) {
            // Typically a different adapter than the game's; the CPU path still works.
            LogPrintf("Presenter: opening shared slot failed hr=0x%08lX, switching to CPU path", (unsigned long)hr);
            ReleasePresenterSlots();
            m_gpuDisabled = true;
            return;
        }
        ps.handle = handle;
    }

    D3D11_TEXTURE2D_DESC td{};
    ps.tex->GetDesc(&td);
    const float tw = (float)td.Width, th = (float)td.Height;

    // Leia SR: each eye at full output resolution, side by side in a 2W x H pair
    // that the SR runtime weaves. Whenever it can't, plain side-by-side is shown.
    const bool leiaMode = m_format == FMT_LEIA_SR;
    HWND leiaWindow = m_hwnd;
    if (leiaMode && m_hwnd && m_cfg.leiaWindow != "own") leiaWindow = GetAncestor(m_hwnd, GA_ROOT);
    if (leiaMode) {
        MatchWindowSize();
        if (!m_rtv) return;
        LogLeiaGeometry(leiaWindow);
    }
    const bool weave = leiaMode && m_leia.Ready(m_device, m_context, leiaWindow, m_outW, m_outH);
    const int fmt = leiaMode ? FMT_SBS : m_format;
    const int compW = weave ? (m_outW & ~1) * 2 : m_outW;
    const int compH = weave ? (m_outH & ~1) : m_outH;
    CompositeParams p{};
    for (int e = 0; e < 2; e++) {
        // Eye e occupies the half [e*0.5, e*0.5+0.5] of the slot texture
        const EyeBounds& b = bounds[e];
        p.rect[e][0] = (e + b.uMin) * 0.5f;
        p.rect[e][1] = b.vMin;
        p.rect[e][2] = (e + b.uMax) * 0.5f;
        p.rect[e][3] = b.vMax;
        // Keep bilinear taps inside this eye's half so the eyes never bleed together
        p.clampRect[e][0] = (std::min)(p.rect[e][0], p.rect[e][2]) + 0.5f / tw;
        p.clampRect[e][1] = (std::min)(p.rect[e][1], p.rect[e][3]) + 0.5f / th;
        p.clampRect[e][2] = (std::max)(p.rect[e][0], p.rect[e][2]) - 0.5f / tw;
        p.clampRect[e][3] = (std::max)(p.rect[e][1], p.rect[e][3]) - 0.5f / th;
    }
    p.format = fmt;
    p.outW = compW;
    p.outH = compH;

    {
        // Output pixels covered by one eye, and eye texels per output pixel
        const bool sbs = fmt == FMT_SBS || fmt == FMT_SBS_CROSS;
        const bool tab = fmt == FMT_TAB || fmt == FMT_TAB_CROSS;
        const bool interlaced = fmt == FMT_INTERLACED || fmt == FMT_INTERLACED_REVERSED;
        const bool checker = fmt == FMT_CHECKERBOARD;
        const float regionW = (float)(sbs ? compW / 2 : compW);
        const float regionH = (float)(tab ? compH / 2 : compH);
        const float texPerOutX = 0.5f * tw * fabsf(bounds[0].uMax - bounds[0].uMin) / regionW;
        const float texPerOutY = th * fabsf(bounds[0].vMax - bounds[0].vMin) / regionH;
        const float decX = checker ? 1.41f : 1.0f;
        const float decY = interlaced ? 2.0f : (checker ? 1.41f : 1.0f);
        const float strength = m_cfg.interlaceFilter;
        // Tent half-width in output pixels: 1 when minifying, the decimation step
        // (scaled by InterlaceFilter) when the format drops rows/columns.
        auto halfWidth = [&](float texPerOut, float dec) {
            float h = texPerOut > 1.01f ? 1.0f : 0.0f;
            if (dec > 1.0f && strength > 0.0f) h = (std::max)(h, dec * strength);
            return h;
        };
        auto taps = [](float half, float texPerOut) {
            if (half <= 0.0f) return 1;
            int n = (int)ceilf(2.0f * half * (std::max)(texPerOut, 1.0f));
            return n < 2 ? 2 : (n > 8 ? 8 : n);
        };
        const float hx = halfWidth(texPerOutX, decX), hy = halfWidth(texPerOutY, decY);
        p.filt[0] = hx / regionW;
        p.filt[1] = hy / regionH;
        p.filt[2] = (float)taps(hx, texPerOutX);
        p.filt[3] = (float)taps(hy, texPerOutY);
    }
    // Crosstalk cancellation is for glasses; an SR panel handles its own
    p.color[0] = leiaMode ? 0.0f : m_crosstalk.load();
    p.color[1] = leiaMode ? 0.0f : m_cfg.crosstalkLift;
    p.color[2] = m_cfg.brightness > 0.0f ? m_cfg.brightness : 1.0f;
    p.color[3] = m_cfg.gamma > 0.0f ? m_cfg.gamma : 1.0f;
    float osdAlpha = 0.0f;
    UpdateOsdTexture(osdAlpha);
    if (osdAlpha > 0.0f) {
        // Every format shows each eye over the full screen, so size the box in
        // eye uv using the screen aspect to keep the text undistorted.
        const float uw = 0.30f;
        const float uh = uw * (float)m_outW / (float)m_outH * (float)kOsdH / (float)kOsdW;
        p.osdRect[0] = 0.5f - uw * 0.5f;
        p.osdRect[1] = 0.08f;
        p.osdRect[2] = 0.5f + uw * 0.5f;
        p.osdRect[3] = 0.08f + uh;
        p.osdParams[0] = osdAlpha;
    }

    if (ps.mutex->AcquireSync(0, 100) != S_OK) return;
    // Draws both eyes into target with the current params
    auto composite = [&](ID3D11RenderTargetView* target, int w, int h) {
        m_context->UpdateSubresource(m_cb, 0, nullptr, &p, 0, 0);
        D3D11_VIEWPORT vp{ 0.0f, 0.0f, (float)w, (float)h, 0.0f, 1.0f };
        m_context->OMSetRenderTargets(1, &target, nullptr);
        m_context->RSSetViewports(1, &vp);
        m_context->IASetInputLayout(nullptr);
        m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        m_context->VSSetShader(m_vs, nullptr, 0);
        m_context->PSSetShader(m_ps, nullptr, 0);
        m_context->PSSetConstantBuffers(0, 1, &m_cb);
        m_context->PSSetSamplers(0, 1, &m_sampler);
        ID3D11ShaderResourceView* srvs[2] = { ps.srv, m_osdSrv };
        m_context->PSSetShaderResources(0, 2, srvs);
        m_context->Draw(3, 0);
    };
    composite(weave ? m_leia.PairRtv() : m_rtv, compW, compH);
    if (m_psAnalyze && m_cfg.menuSeparation >= 0.0f && (m_anaFrame++ % 6) == 0) {
        D3D11_VIEWPORT av{ 0.0f, 0.0f, 320.0f, 45.0f, 0.0f, 1.0f };
        m_context->OMSetRenderTargets(1, &m_anaRtv, nullptr);
        m_context->RSSetViewports(1, &av);
        m_context->PSSetShader(m_psAnalyze, nullptr, 0);
        m_context->Draw(3, 0);
        m_context->CopyResource(m_anaStaging[m_anaWrite], m_anaTex);
        m_anaWrite = (m_anaWrite + 1) % 3;
        if (m_anaPending < 3) m_anaPending++;
    }
    if (weave) {
        // Last, because the weaver leaves its own pipeline state behind (WeaveInto
        // resets it). The weaver samples the pair, so it is no longer a target.
        m_context->OMSetRenderTargets(0, nullptr, nullptr);
        ID3D11Texture2D* backBuffer = nullptr;
        bool woven = false;
        if (SUCCEEDED(AcquireBackBuffer(&backBuffer))) {
            woven = m_leia.WeaveInto(m_context, backBuffer);
            backBuffer->Release();
        }
        if (!woven) {
            // A failed weave must not blank the screen: plain side-by-side instead
            p.outW = m_outW;
            p.outH = m_outH;
            composite(m_rtv, m_outW, m_outH);
        }
    }
    ID3D11ShaderResourceView* nullSrvs[2] = {};
    m_context->PSSetShaderResources(0, 2, nullSrvs);
    ps.mutex->ReleaseSync(0);

    RunMenuAnalysis();

    m_framesSeen++;
    if (DumpDue()) {
        // LeiaSR: the weaver's input too, the full-resolution side-by-side pair
        if (weave && m_leia.PairTexture()) DumpTexture(m_leia.PairTexture(), "pair");
        DumpBackBuffer();
    }

    HRESULT hr = PresentFrame();
    if (FAILED(hr)) {
        LogPrintf("Presenter: Present failed hr=0x%08lX", (unsigned long)hr);
    }
}

void Presenter::ShowOsd(const char* text) {
    std::lock_guard<std::mutex> lk(m_mtx);
    if (m_osdText != text) {
        m_osdText = text;
        m_osdVersion++;
    }
    m_osdTick = GetTickCount64();
}

// Renders the OSD line with GDI into a small texture when it changes and
// returns the current opacity (visible 2 s after the last update, then fades).
void Presenter::UpdateOsdTexture(float& alpha) {
    std::string text;
    uint64_t version;
    unsigned long long tick;
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        text = m_osdText;
        version = m_osdVersion;
        tick = m_osdTick;
    }
    alpha = 0.0f;
    if (!version) return;
    unsigned long long age = GetTickCount64() - tick;
    if (age >= 2500) return;
    alpha = age < 2000 ? 1.0f : 1.0f - (float)(age - 2000) / 500.0f;

    if (!m_osdTex) {
        D3D11_TEXTURE2D_DESC td{};
        td.Width = kOsdW;
        td.Height = kOsdH;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(m_device->CreateTexture2D(&td, nullptr, &m_osdTex)) ||
            FAILED(m_device->CreateShaderResourceView(m_osdTex, nullptr, &m_osdSrv))) {
            SafeRelease(m_osdTex);
            alpha = 0.0f;
            return;
        }
    }
    if (version == m_osdBuiltVersion) return;
    m_osdBuiltVersion = version;

    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = kOsdW;
    bi.bmiHeader.biHeight = -kOsdH;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!dc || !bmp || !bits) {
        if (bmp) DeleteObject(bmp);
        if (dc) DeleteDC(dc);
        return;
    }
    HGDIOBJ oldBmp = SelectObject(dc, bmp);
    HFONT font = CreateFontW(kOsdH * 3 / 4, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, FIXED_PITCH, L"Consolas");
    HGDIOBJ oldFont = SelectObject(dc, font);
    memset(bits, 0, (size_t)kOsdW * kOsdH * 4);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    RECT rc{ 0, 0, kOsdW, kOsdH };
    DrawTextA(dc, text.c_str(), -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    GdiFlush();
    m_context->UpdateSubresource(m_osdTex, 0, nullptr, bits, kOsdW * 4, 0);
    SelectObject(dc, oldFont);
    SelectObject(dc, oldBmp);
    DeleteObject(font);
    DeleteObject(bmp);
    DeleteDC(dc);
}

// Reads back the oldest pending analysis (two analyses behind, so the GPU is long
// done with it) without ever blocking, then advances the separation blend.
void Presenter::RunMenuAnalysis() {
    LARGE_INTEGER now, freq;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&freq);
    float dt = m_lastBlendTime.QuadPart ? (float)((double)(now.QuadPart - m_lastBlendTime.QuadPart) / freq.QuadPart)
                                        : 0.0f;
    m_lastBlendTime = now;

    if (m_anaPending >= 3) {
        ID3D11Texture2D* st = m_anaStaging[m_anaWrite];  // oldest of the ring
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (SUCCEEDED(m_context->Map(st, 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped))) {
            AnalyzeEyes((const uint8_t*)mapped.pData, mapped.RowPitch);
            m_context->Unmap(st, 0);
            m_anaPending = 2;
        }
    }

    // Glide over ~0.4 s so switching never snaps the depth
    float target = m_inMenu.load() ? 1.0f : 0.0f;
    float b = m_menuBlend.load();
    float step = dt / 0.4f;
    b = b < target ? (std::min)(target, b + step) : (std::max)(target, b - step);
    m_menuBlend = b;
}

void Presenter::AnalyzeEyes(const uint8_t* data, UINT pitch) {
    const int W = 160, H = 45, M = 40;
    static float img[2][45][160];
    static float tmp[45][160];
    for (int e = 0; e < 2; e++)
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) img[e][y][x] = data[(size_t)y * pitch + (size_t)(e * W + x) * 4];
    // Blur away sub-cell detail (fine text) so only real geometry differences remain
    static const float k[5] = { 1 / 16.f, 4 / 16.f, 6 / 16.f, 4 / 16.f, 1 / 16.f };
    for (int pass = 0; pass < 2; pass++) {
        for (int e = 0; e < 2; e++) {
            for (int y = 0; y < H; y++)
                for (int x = 0; x < W; x++) {
                    float a = 0;
                    for (int i = -2; i <= 2; i++) {
                        int xx = pass == 0 ? (std::min)(W - 1, (std::max)(0, x + i)) : x;
                        int yy = pass == 1 ? (std::min)(H - 1, (std::max)(0, y + i)) : y;
                        a += k[i + 2] * img[e][yy][xx];
                    }
                    tmp[y][x] = a;
                }
            memcpy(img[e], tmp, sizeof(tmp));
        }
    }
    float energy = 0;
    for (int y = 0; y < H; y++)
        for (int x = M; x < W - M; x++) energy += img[0][y][x];
    energy /= (float)(H * (W - 2 * M));
    if (energy < 1.5f) return;  // black/loading screen: keep the current state

    // Best sub-pixel horizontal shift of the right eye onto the left eye
    float best = 1e30f, bestShift = 0;
    for (float o = -38.0f; o <= 38.0f; o += 0.25f) {
        int io = (int)floorf(o);
        float f = o - io;
        float sum = 0;
        for (int y = 0; y < H; y++)
            for (int x = M; x < W - M; x++) {
                int x0 = x + io;
                float r = img[1][y][x0] * (1 - f) + img[1][y][x0 + 1] * f;
                sum += fabsf(img[0][y][x] - r);
            }
        if (sum < best) {
            best = sum;
            bestShift = o;
        }
    }
    float residual = best / (float)(H * (W - 2 * M)) / energy;

    // Measured over a session: menus sit at 0.02-0.045, gameplay mostly 0.08-0.14
    // but dips to ~0.04 for single samples, and the main menu's intro (ships past
    // the camera) spikes to ~0.1 for a second or two. Sparse stretches of gameplay
    // (mostly far geometry) averaged as low as 0.053 and were taken for menus, which
    // flattened the game: entering menus now needs < 0.035. A ~2.5 s average (samples at
    // ~10 Hz) with hysteresis rides through both.
    const float alpha = 0.039f;
    m_residualAvg += alpha * (residual - m_residualAvg);
    bool menu = m_inMenu.load();
    if (!menu && m_residualAvg < 0.035f) {
        m_inMenu = true;
        LogPrintf("Scene: menu (avg residual %.3f, shift %.2f)", m_residualAvg, bestShift);
    } else if (menu && m_residualAvg > 0.085f) {
        m_inMenu = false;
        LogPrintf("Scene: gameplay (avg residual %.3f, shift %.2f)", m_residualAvg, bestShift);
    }
    if (m_cfg.logFrameCalls >= 2)
        LogPrintf("MenuDetect: residual %.3f shift %.2f energy %.1f", residual, bestShift, energy);
}

float Presenter::EffectiveSeparation(float gameplaySeparation) {
    if (m_cfg.menuSeparation < 0.0f || m_gpuDisabled.load()) return gameplaySeparation;
    float b = m_menuBlend.load();
    return gameplaySeparation + (m_cfg.menuSeparation - gameplaySeparation) * b;
}

// Leia SR weaves per physical pixel, so the woven image has to reach the screen
// 1:1. The swapchain starts at the monitor's mode, but the window it shows in is
// the game's and can differ (another resolution, a resize): follow the window's
// client size instead of letting DXGI stretch the weave.
void Presenter::MatchWindowSize() {
    if (!m_swapChain || !m_hwnd) return;
    RECT rc{};
    GetClientRect(m_hwnd, &rc);
    const int w = rc.right, h = rc.bottom;
    if (w < 64 || h < 64 || (w == m_outW && h == m_outH)) return;
    m_context->OMSetRenderTargets(0, nullptr, nullptr);
    m_context->ClearState();
    SafeRelease(m_rtv);
    m_context->Flush();
    HRESULT hr = m_swapChain->ResizeBuffers(0, (UINT)w, (UINT)h, DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr)) {
        LogPrintf("Presenter: ResizeBuffers %dx%d failed hr=0x%08lX, keeping %dx%d", w, h, (unsigned long)hr, m_outW,
                  m_outH);
    } else {
        LogPrintf("Presenter: output %dx%d -> %dx%d, the size of the window it is shown in", m_outW, m_outH, w, h);
        m_outW = w;
        m_outH = h;
        // The CPU fallback uploads a whole frame into this texture
        D3D11_TEXTURE2D_DESC td{};
        if (m_dynamicTex) m_dynamicTex->GetDesc(&td);
        td.Width = (UINT)w;
        td.Height = (UINT)h;
        SafeRelease(m_dynamicTex);
        if (SUCCEEDED(m_device->CreateTexture2D(&td, nullptr, &m_dynamicTex))) {
            m_dynamicW = (UINT)w;
            m_dynamicH = (UINT)h;
        }
    }
    ID3D11Texture2D* backBuffer = nullptr;
    if (SUCCEEDED(AcquireBackBuffer(&backBuffer))) {
        m_device->CreateRenderTargetView(backBuffer, nullptr, &m_rtv);
        backBuffer->Release();
    }
}

// One line with everything the weave's alignment depends on, logged when it
// changes. A tester's log then shows whether the woven image lands 1:1 on the
// panel: the display mode, the window the weaver was given, our view and the
// swapchain must all agree.
void Presenter::LogLeiaGeometry(HWND weaverWindow) {
    if (!m_hwnd || m_leiaGeometryLogs >= 20) return;
    using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
    static auto getDpi = reinterpret_cast<GetDpiForWindowFn>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
    HMONITOR mon = MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(mon, &mi);
    DEVMODEW cur{}, reg{};
    cur.dmSize = reg.dmSize = sizeof(DEVMODEW);
    EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &cur);
    EnumDisplaySettingsW(mi.szDevice, ENUM_REGISTRY_SETTINGS, &reg);
    auto clientOnScreen = [](HWND h, RECT& r) {
        GetClientRect(h, &r);
        POINT o{ 0, 0 };
        ClientToScreen(h, &o);
        OffsetRect(&r, o.x, o.y);
    };
    RECT ours{}, given{};
    clientOnScreen(m_hwnd, ours);
    clientOnScreen(weaverWindow, given);
    DWORD givenPid = 0;
    GetWindowThreadProcessId(weaverWindow, &givenPid);
    UINT scW = 0, scH = 0;
    DXGI_SWAP_CHAIN_DESC sd{};
    if (m_swapChain && SUCCEEDED(m_swapChain->GetDesc(&sd))) {
        scW = sd.BufferDesc.Width;
        scH = sd.BufferDesc.Height;
    }
    const RECT& mr = mi.rcMonitor;
    char text[640];
    sprintf_s(text,
              "monitor %ls (%ld,%ld)-(%ld,%ld) mode %lux%lu (desktop %lux%lu) dpi %u | weaver window %p pid %lu "
              "client (%ld,%ld)-(%ld,%ld) | 3D view (%ld,%ld)-(%ld,%ld) | swapchain %ux%u",
              mi.szDevice, mr.left, mr.top, mr.right, mr.bottom, cur.dmPelsWidth, cur.dmPelsHeight, reg.dmPelsWidth,
              reg.dmPelsHeight, getDpi ? getDpi(m_hwnd) : 0u, (void*)weaverWindow, givenPid, given.left, given.top,
              given.right, given.bottom, ours.left, ours.top, ours.right, ours.bottom, scW, scH);
    if (m_leiaGeometry == text) return;
    m_leiaGeometry = text;
    m_leiaGeometryLogs++;
    LogPrintf("Leia SR geometry: %s", text);
    // The checks that matter for a clean weave, spelled out
    if (!EqualRect(&ours, &mr))
        LogPrintf("Leia SR: the 3D view does not cover the whole monitor");
    if ((long)cur.dmPelsWidth != mr.right - mr.left || (long)cur.dmPelsHeight != mr.bottom - mr.top)
        LogPrintf("Leia SR: the monitor rectangle differs from its display mode (DPI scaling?)");
    if (reg.dmPelsWidth && (cur.dmPelsWidth != reg.dmPelsWidth || cur.dmPelsHeight != reg.dmPelsHeight))
        LogPrintf("Leia SR: the display runs %lux%lu, not the desktop's %lux%lu: the panel is scaling",
                  cur.dmPelsWidth, cur.dmPelsHeight, reg.dmPelsWidth, reg.dmPelsHeight);
    if (weaverWindow != m_hwnd && !EqualRect(&given, &ours))
        LogPrintf("Leia SR: the weaver's window differs from the 3D view (try LeiaWindow=own)");
    if ((long)scW != ours.right - ours.left || (long)scH != ours.bottom - ours.top)
        LogPrintf("Leia SR: the swapchain is stretched to the window");
}

HRESULT Presenter::AcquireBackBuffer(ID3D11Texture2D** out) {
    if (m_offscreen) {
        m_offscreen->AddRef();
        *out = m_offscreen;
        return S_OK;
    }
    if (!m_swapChain) return E_FAIL;
    return m_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)out);
}

// Headless has no vsync to block on, so pace to the configured refresh rate
// to keep the game's frame pacing (and timing) the same as on a real display.
HRESULT Presenter::PresentFrame() {
    if (m_swapChain) return m_swapChain->Present(m_cfg.vsync ? 1 : 0, 0);
    m_context->Flush();
    LARGE_INTEGER freq, now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&now);
    const LONGLONG period = freq.QuadPart / (m_cfg.fps > 0 ? m_cfg.fps : 60);
    if (m_nextFrame.QuadPart == 0 || now.QuadPart - m_nextFrame.QuadPart > period) m_nextFrame = now;
    m_nextFrame.QuadPart += period;
    LONGLONG wait100ns = (m_nextFrame.QuadPart - now.QuadPart) * 10000000 / freq.QuadPart;
    if (wait100ns > 0) {
        static HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                                     TIMER_ALL_ACCESS);
        LARGE_INTEGER due;
        due.QuadPart = -wait100ns;
        if (timer && SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE))
            WaitForSingleObject(timer, 100);
        else
            Sleep((DWORD)(wait100ns / 10000));
    }
    return S_OK;
}

// DumpAfter/DumpEvery count presented frames. DumpAfterSec/DumpEverySec count
// seconds since the first frame instead, which stays on time when the machine is
// busy and the game runs below the display rate.
bool Presenter::DumpDue() {
    if (m_dumpRemaining <= 0) return false;
    if (m_cfg.dumpAfterSec > 0.0f) {
        const ULONGLONG now = GetTickCount64();
        if (!m_dumpStartTick) m_dumpStartTick = now;
        const double t = (now - m_dumpStartTick) / 1000.0;
        const double every = m_cfg.dumpEverySec > 0.0f ? m_cfg.dumpEverySec : 1.0;
        if (t < m_cfg.dumpAfterSec + m_dumpsTimed * every) return false;
        m_dumpsTimed++;
        return true;
    }
    return m_framesSeen > (unsigned long long)m_dumpAfter &&
           ((m_framesSeen - (unsigned long long)m_dumpAfter) % (unsigned long long)m_dumpEvery) == 0;
}

void Presenter::DumpBackBuffer() {
    ID3D11Texture2D* backBuffer = nullptr;
    if (FAILED(AcquireBackBuffer(&backBuffer))) return;
    DumpTexture(backBuffer, "frame");
    backBuffer->Release();
}

void Presenter::DumpTexture(ID3D11Texture2D* tex, const char* kind) {
    D3D11_TEXTURE2D_DESC sd{};
    tex->GetDesc(&sd);
    sd.Usage = D3D11_USAGE_STAGING;
    sd.BindFlags = 0;
    sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    sd.MiscFlags = 0;
    ID3D11Texture2D* staging = nullptr;
    if (SUCCEEDED(m_device->CreateTexture2D(&sd, nullptr, &staging))) {
        m_context->CopyResource(staging, tex);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (SUCCEEDED(m_context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped))) {
            std::vector<uint8_t> pixels;
            ConvertToBgra(mapped, sd.Width, sd.Height, DXGI_FORMAT_B8G8R8A8_UNORM, pixels);
            m_context->Unmap(staging, 0);
            SaveDump(pixels.data(), (int)sd.Width, (int)sd.Height, kind);
        }
        staging->Release();
    }
}

void Presenter::CleanupGraphics() {
    m_leia.Release();  // the weaver goes before the device and the textures it sampled
    for (auto& st : m_anaStaging) SafeRelease(st);
    SafeRelease(m_anaRtv);
    SafeRelease(m_anaTex);
    SafeRelease(m_psAnalyze);
    SafeRelease(m_offscreen);
    ReleasePresenterSlots();
    SafeRelease(m_osdSrv);
    SafeRelease(m_osdTex);
    SafeRelease(m_rtv);
    SafeRelease(m_sampler);
    SafeRelease(m_cb);
    SafeRelease(m_ps);
    SafeRelease(m_vs);
    if (m_swapChain) { m_swapChain->SetFullscreenState(FALSE, nullptr); m_swapChain->Release(); m_swapChain = nullptr; }
    if (m_dynamicTex) { m_dynamicTex->Release(); m_dynamicTex = nullptr; }
    if (m_context) { m_context->Release(); m_context = nullptr; }
    if (m_device) { m_device->Release(); m_device = nullptr; }
    if (m_hwnd) { DestroyWindow(m_hwnd); m_hwnd = nullptr; }
    LogPrintf("Presenter: graphics cleaned up");
}

static inline void SampleBilinear(const uint8_t* src, int w, int h, float u, float v, uint8_t out[4]) {
    float px = u * w - 0.5f;
    float py = v * h - 0.5f;
    if (px < 0.0f) px = 0.0f;
    if (py < 0.0f) py = 0.0f;
    if (px > (float)(w - 1)) px = (float)(w - 1);
    if (py > (float)(h - 1)) py = (float)(h - 1);
    int x0 = (int)px;
    int y0 = (int)py;
    int x1 = x0 + 1 < w ? x0 + 1 : x0;
    int y1 = y0 + 1 < h ? y0 + 1 : y0;
    float fx = px - x0;
    float fy = py - y0;
    const uint8_t* p00 = src + ((size_t)y0 * w + x0) * 4;
    const uint8_t* p10 = src + ((size_t)y0 * w + x1) * 4;
    const uint8_t* p01 = src + ((size_t)y1 * w + x0) * 4;
    const uint8_t* p11 = src + ((size_t)y1 * w + x1) * 4;
    for (int c = 0; c < 4; c++) {
        float top = p00[c] + (p10[c] - p00[c]) * fx;
        float bot = p01[c] + (p11[c] - p01[c]) * fx;
        float val = top + (bot - top) * fy;
        int iv = (int)(val + 0.5f);
        out[c] = (uint8_t)(iv < 0 ? 0 : (iv > 255 ? 255 : iv));
    }
}

void Presenter::Composite(uint8_t* dst, UINT dstPitch) {
    const int W = m_outW;
    const int H = m_outH;
    const int ew = m_localEyeW;
    const int eh = m_localEyeH;
    const uint8_t* eye0 = m_localEye[0].data();
    const uint8_t* eye1 = m_localEye[1].data();
    const EyeBounds b0 = m_localBounds[0];
    const EyeBounds b1 = m_localBounds[1];
    // The CPU fallback has no weaver: Leia SR shows plain side-by-side
    const int fmt = m_format == FMT_LEIA_SR ? FMT_SBS : m_format;

    auto fetchEye = [&](int e, float u, float v, uint8_t out[4]) {
        const EyeBounds& b = (e == 0) ? b0 : b1;
        const uint8_t* src = (e == 0) ? eye0 : eye1;
        float su = b.uMin + u * (b.uMax - b.uMin);
        float sv = b.vMin + v * (b.vMax - b.vMin);
        SampleBilinear(src, ew, eh, su, sv, out);
    };

    concurrency::parallel_for(0, H, [&](int y) {
        uint8_t* row = dst + (size_t)y * dstPitch;
        float v = (y + 0.5f) / (float)H;
        for (int x = 0; x < W; x++) {
            uint8_t* px = row + (size_t)x * 4;
            uint8_t c[4];
            float u = (x + 0.5f) / (float)W;
            switch (fmt) {
            case FMT_SBS:
            case FMT_SBS_CROSS: {
                int half = W / 2;
                int e;
                if (x < half) {
                    e = 0;
                    u = (x + 0.5f) / (float)half;
                } else {
                    e = 1;
                    u = (x - half + 0.5f) / (float)(W - half);
                }
                if (fmt == FMT_SBS_CROSS) e ^= 1;
                fetchEye(e, u, v, c);
                memcpy(px, c, 4);
                break;
            }
            case FMT_TAB:
            case FMT_TAB_CROSS: {
                int half = H / 2;
                int e;
                float vv;
                if (y < half) {
                    e = 0;
                    vv = (y + 0.5f) / (float)half;
                } else {
                    e = 1;
                    vv = (y - half + 0.5f) / (float)(H - half);
                }
                if (fmt == FMT_TAB_CROSS) e ^= 1;
                fetchEye(e, u, vv, c);
                memcpy(px, c, 4);
                break;
            }
            case FMT_INTERLACED:
            case FMT_INTERLACED_REVERSED: {
                int e = y & 1;
                if (fmt == FMT_INTERLACED_REVERSED) e ^= 1;
                fetchEye(e, u, v, c);
                memcpy(px, c, 4);
                break;
            }
            case FMT_CHECKERBOARD: {
                int e = (x ^ y) & 1;
                fetchEye(e, u, v, c);
                memcpy(px, c, 4);
                break;
            }
            case FMT_MONO_LEFT:
                fetchEye(0, u, v, c);
                memcpy(px, c, 4);
                break;
            case FMT_MONO_RIGHT:
                fetchEye(1, u, v, c);
                memcpy(px, c, 4);
                break;
            default:
                fetchEye(0, u, v, c);
                memcpy(px, c, 4);
                break;
            }
        }
    });
}

static void SaveBmp(const char* path, const uint8_t* data, int w, int h) {
    BITMAPFILEHEADER fh{};
    BITMAPINFOHEADER ih{};
    fh.bfType = 0x4D42;
    fh.bfOffBits = sizeof(fh) + sizeof(ih);
    fh.bfSize = fh.bfOffBits + (DWORD)w * h * 4;
    ih.biSize = sizeof(ih);
    ih.biWidth = w;
    ih.biHeight = -h;
    ih.biPlanes = 1;
    ih.biBitCount = 32;
    ih.biCompression = BI_RGB;
    ih.biSizeImage = (DWORD)w * h * 4;
    FILE* f = nullptr;
    if (fopen_s(&f, path, "wb") != 0 || !f) return;
    fwrite(&fh, sizeof(fh), 1, f);
    fwrite(&ih, sizeof(ih), 1, f);
    fwrite(data, (size_t)w * h * 4, 1, f);
    fclose(f);
}

void Presenter::SaveDump(const uint8_t* data, int w, int h, const char* kind) {
    char module[MAX_PATH] = {};
    HMODULE self = nullptr;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCSTR)&SaveBmp, &self))
        return;
    GetModuleFileNameA(self, module, MAX_PATH);
    char* slash = strrchr(module, '\\');
    if (slash) *slash = 0;
    char path[MAX_PATH];
    // Extra captures of the same frame (the LeiaSR pair) share its number
    const bool frame = strcmp(kind, "frame") == 0;
    sprintf_s(path, "%s\\rez3d_%s_%05u.bmp", module, kind, m_dumpIndex);
    SaveBmp(path, data, w, h);
    if (frame) {
        m_dumpIndex++;
        m_dumpRemaining--;
    }
    LogPrintf("Presenter: dumped %s", path);
}

void Presenter::PresentLatest() {
    if (!m_device || (!m_swapChain && !m_offscreen) || !m_dynamicTex) return;

    if (m_localEye[0].size() == m_localEye[1].size() && !m_localEye[0].empty()) {
        static unsigned diffCount = 0;
        if (diffCount < 12 || (m_framesSeen % 300) == 0) {
            uint64_t h0 = 1469598103934665603ull, h1 = 1469598103934665603ull;
            uint64_t sad = 0;
            size_t samples = 0;
            for (size_t i = 0; i < m_localEye[0].size(); i += 4) {
                h0 = (h0 ^ m_localEye[0][i]) * 1099511628211ull;
                h1 = (h1 ^ m_localEye[1][i]) * 1099511628211ull;
                int d = (int)m_localEye[0][i] - (int)m_localEye[1][i];
                if (d < 0) d = -d;
                sad += (uint64_t)d;
                samples++;
            }
            LogPrintf("EyeDiff #%u mean=%.4f hashes=%016llX/%016llX size=%zux%zu", diffCount,
                      samples ? (double)sad / samples : 0.0, (unsigned long long)h0, (unsigned long long)h1,
                      (size_t)m_localEyeW, (size_t)m_localEyeH);
            diffCount++;
        }
    }

    if (m_cpuImage.size() < (size_t)m_outW * m_outH * 4) m_cpuImage.resize((size_t)m_outW * m_outH * 4);
    Composite(m_cpuImage.data(), (UINT)m_outW * 4);

    m_framesSeen++;
    if (DumpDue()) {
        SaveDump(m_cpuImage.data(), m_outW, m_outH);
    }

    m_context->UpdateSubresource(m_dynamicTex, 0, nullptr, m_cpuImage.data(), (UINT)m_outW * 4, 0);

    ID3D11Texture2D* backBuffer = nullptr;
    if (FAILED(AcquireBackBuffer(&backBuffer))) {
        return;
    }
    m_context->CopyResource(backBuffer, m_dynamicTex);
    backBuffer->Release();

    HRESULT hr = PresentFrame();
    if (FAILED(hr)) {
        LogPrintf("Presenter: Present failed hr=0x%08lX", (unsigned long)hr);
    }
}
