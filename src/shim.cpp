// Fake OpenVR runtime for Rez Infinite ("3D monitor port").
// Implements enough of the OpenVR client API for the game to render stereoscopic
// frames and hands them to the stereo presenter instead of a VR compositor.

#include "openvr_compat.h"

#include <windows.h>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <atomic>
#include <mutex>
#include <string>

#include "audio.h"
#include "config.h"
#include "log.h"
#include "presenter.h"

// Per-frame call statistics (LogFrameCalls=2 logs whenever the pattern changes)
static const int kMaxSites = 32;
static std::atomic<unsigned> g_siteCount[kMaxSites];
static const char* g_siteName[kMaxSites];
static std::atomic<int> g_siteNext{0};
static int RegisterSite(const char* name) {
    int i = g_siteNext++;
    if (i < kMaxSites) g_siteName[i] = name;
    return i;
}
static float g_lastNear = 0.0f, g_lastFar = 0.0f;

#define TRACE_FN(...) do { static int s_site = RegisterSite(__FUNCTION__); if (s_site < kMaxSites) g_siteCount[s_site]++; static unsigned s_traceCount = 0; if (s_traceCount < 5 || (s_traceCount % 500) == 0) LogPrintf(__VA_ARGS__); ++s_traceCount; } while (0)

// Added after OpenVR 1.0.2 (used by IVRSystem_015's GetHiddenAreaMesh)
enum EHiddenAreaMeshType {
    k_eHiddenAreaMesh_Standard = 0,
    k_eHiddenAreaMesh_Inverse = 1,
    k_eHiddenAreaMesh_LineLoop = 2,
};

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static HMODULE g_hModule = nullptr;
static char g_dllDir[MAX_PATH] = {};
static StereoConfig g_cfg;
static uint32_t g_eyeW = 960, g_eyeH = 540;
static float g_tanH = 1.0f, g_tanV = 0.75f;
static intptr_t g_token = 0x52455A33;
static bool g_initialized = false;
static uint32_t g_trackingSpace = 0;  // TrackingUniverseSeated
static uint64_t g_frameCounter = 0;
static LARGE_INTEGER g_qpcFreq{};
static LARGE_INTEGER g_lastVsync{};
static LARGE_INTEGER g_lastWait{};
static bool g_mouseValid = false;
static POINT g_lastMouse{};
static float g_yawDeg = 0.0f;
static float g_pitchDeg = 0.0f;
static bool g_suspended = false;
static bool g_noPresenter = false;
static float g_screenWidthMM = 600.0f;
static bool g_stereoOn = true;

static uint32_t g_submitCount = 0;
static uint32_t g_waitCount = 0;
static uint32_t g_lastSubmitLog = 0;
static bool g_loggedTexture = false;

// Minimal VR event queue for scene/input focus notifications.
static CRITICAL_SECTION g_eventCs;
static bool g_eventCsInit = false;
static VREvent_t g_events[16] = {};
static int g_eventHead = 0;
static int g_eventTail = 0;

static void EnsureEventQueue() {
    if (!g_eventCsInit) {
        InitializeCriticalSection(&g_eventCs);
        g_eventCsInit = true;
    }
}

static void PushEvent(uint32_t type, uint32_t pid = 0, uint32_t oldPid = 0, bool forced = false) {
    EnsureEventQueue();
    EnterCriticalSection(&g_eventCs);
    int next = (g_eventTail + 1) % (int)(sizeof(g_events) / sizeof(g_events[0]));
    if (next != g_eventHead) {
        VREvent_t ev{};
        ev.eventType = type;
        ev.trackedDeviceIndex = 0;
        ev.eventAgeSeconds = 0.0f;
        ev.data.process.pid = pid;
        ev.data.process.oldPid = oldPid;
        ev.data.process.bForced = forced;
        g_events[g_eventTail] = ev;
        g_eventTail = next;
        LogPrintf("PushEvent type=%u pid=%u oldPid=%u", type, pid, oldPid);
    }
    LeaveCriticalSection(&g_eventCs);
}

static bool PopEvent(VREvent_t* out, uint32_t cb) {
    EnsureEventQueue();
    bool have = false;
    EnterCriticalSection(&g_eventCs);
    if (g_eventHead != g_eventTail) {
        if (out && cb) {
            uint32_t n = cb < sizeof(VREvent_t) ? cb : (uint32_t)sizeof(VREvent_t);
            memcpy(out, &g_events[g_eventHead], n);
        }
        g_eventHead = (g_eventHead + 1) % (int)(sizeof(g_events) / sizeof(g_events[0]));
        have = true;
    }
    LeaveCriticalSection(&g_eventCs);
    return have;
}

static uint32_t g_focusEventType = VREvent_InputFocusCaptured;

static void QueueFocusEvents() {
    if (!g_focusEventType) return;
    DWORD pid = GetCurrentProcessId();
    PushEvent(g_focusEventType, pid, 0, false);
}

// ---------------------------------------------------------------------------
// Version-specific IVRSystem layout for 015 (signatures changed after 012)
// ---------------------------------------------------------------------------
struct VR_IVRSystem015_FnTable {
    void(OPENVR_FNTABLE_CALLTYPE* GetRecommendedRenderTargetSize)(uint32_t*, uint32_t*);
    struct HmdMatrix44_t(OPENVR_FNTABLE_CALLTYPE* GetProjectionMatrix)(EVREye, float, float);
    void(OPENVR_FNTABLE_CALLTYPE* GetProjectionRaw)(EVREye, float*, float*, float*, float*);
    bool(OPENVR_FNTABLE_CALLTYPE* ComputeDistortion)(EVREye, float, float, struct DistortionCoordinates_t*);
    struct HmdMatrix34_t(OPENVR_FNTABLE_CALLTYPE* GetEyeToHeadTransform)(EVREye);
    bool(OPENVR_FNTABLE_CALLTYPE* GetTimeSinceLastVsync)(float*, uint64_t*);
    int32_t(OPENVR_FNTABLE_CALLTYPE* GetD3D9AdapterIndex)();
    void(OPENVR_FNTABLE_CALLTYPE* GetDXGIOutputInfo)(int32_t*);
    bool(OPENVR_FNTABLE_CALLTYPE* IsDisplayOnDesktop)();
    bool(OPENVR_FNTABLE_CALLTYPE* SetDisplayVisibility)(bool);
    void(OPENVR_FNTABLE_CALLTYPE* GetDeviceToAbsoluteTrackingPose)(ETrackingUniverseOrigin, float,
                                                                   struct TrackedDevicePose_t*, uint32_t);
    void(OPENVR_FNTABLE_CALLTYPE* ResetSeatedZeroPose)();
    struct HmdMatrix34_t(OPENVR_FNTABLE_CALLTYPE* GetSeatedZeroPoseToStandingAbsoluteTrackingPose)();
    struct HmdMatrix34_t(OPENVR_FNTABLE_CALLTYPE* GetRawZeroPoseToStandingAbsoluteTrackingPose)();
    uint32_t(OPENVR_FNTABLE_CALLTYPE* GetSortedTrackedDeviceIndicesOfClass)(ETrackedDeviceClass,
                                                                            TrackedDeviceIndex_t*, uint32_t,
                                                                            TrackedDeviceIndex_t);
    EDeviceActivityLevel(OPENVR_FNTABLE_CALLTYPE* GetTrackedDeviceActivityLevel)(TrackedDeviceIndex_t);
    void(OPENVR_FNTABLE_CALLTYPE* ApplyTransform)(struct TrackedDevicePose_t*, struct TrackedDevicePose_t*,
                                                  struct HmdMatrix34_t*);
    TrackedDeviceIndex_t(OPENVR_FNTABLE_CALLTYPE* GetTrackedDeviceIndexForControllerRole)(ETrackedControllerRole);
    ETrackedControllerRole(OPENVR_FNTABLE_CALLTYPE* GetControllerRoleForTrackedDeviceIndex)(TrackedDeviceIndex_t);
    ETrackedDeviceClass(OPENVR_FNTABLE_CALLTYPE* GetTrackedDeviceClass)(TrackedDeviceIndex_t);
    bool(OPENVR_FNTABLE_CALLTYPE* IsTrackedDeviceConnected)(TrackedDeviceIndex_t);
    bool(OPENVR_FNTABLE_CALLTYPE* GetBoolTrackedDeviceProperty)(TrackedDeviceIndex_t, ETrackedDeviceProperty,
                                                                ETrackedPropertyError*);
    float(OPENVR_FNTABLE_CALLTYPE* GetFloatTrackedDeviceProperty)(TrackedDeviceIndex_t, ETrackedDeviceProperty,
                                                                  ETrackedPropertyError*);
    int32_t(OPENVR_FNTABLE_CALLTYPE* GetInt32TrackedDeviceProperty)(TrackedDeviceIndex_t, ETrackedDeviceProperty,
                                                                    ETrackedPropertyError*);
    uint64_t(OPENVR_FNTABLE_CALLTYPE* GetUint64TrackedDeviceProperty)(TrackedDeviceIndex_t, ETrackedDeviceProperty,
                                                                      ETrackedPropertyError*);
    struct HmdMatrix34_t(OPENVR_FNTABLE_CALLTYPE* GetMatrix34TrackedDeviceProperty)(TrackedDeviceIndex_t,
                                                                                    ETrackedDeviceProperty,
                                                                                    ETrackedPropertyError*);
    uint32_t(OPENVR_FNTABLE_CALLTYPE* GetStringTrackedDeviceProperty)(TrackedDeviceIndex_t, ETrackedDeviceProperty,
                                                                      char*, uint32_t, ETrackedPropertyError*);
    char*(OPENVR_FNTABLE_CALLTYPE* GetPropErrorNameFromEnum)(ETrackedPropertyError);
    bool(OPENVR_FNTABLE_CALLTYPE* PollNextEvent)(struct VREvent_t*, uint32_t);
    bool(OPENVR_FNTABLE_CALLTYPE* PollNextEventWithPose)(ETrackingUniverseOrigin, struct VREvent_t*, uint32_t,
                                                         struct TrackedDevicePose_t*);
    char*(OPENVR_FNTABLE_CALLTYPE* GetEventTypeNameFromEnum)(EVREventType);
    struct HiddenAreaMesh_t(OPENVR_FNTABLE_CALLTYPE* GetHiddenAreaMesh)(EVREye, EHiddenAreaMeshType);
    bool(OPENVR_FNTABLE_CALLTYPE* GetControllerState)(TrackedDeviceIndex_t, VRControllerState_t*, uint32_t);
    bool(OPENVR_FNTABLE_CALLTYPE* GetControllerStateWithPose)(ETrackingUniverseOrigin, TrackedDeviceIndex_t,
                                                              VRControllerState_t*, uint32_t,
                                                              struct TrackedDevicePose_t*);
    void(OPENVR_FNTABLE_CALLTYPE* TriggerHapticPulse)(TrackedDeviceIndex_t, uint32_t, unsigned short);
    char*(OPENVR_FNTABLE_CALLTYPE* GetButtonIdNameFromEnum)(EVRButtonId);
    char*(OPENVR_FNTABLE_CALLTYPE* GetControllerAxisTypeNameFromEnum)(EVRControllerAxisType);
    bool(OPENVR_FNTABLE_CALLTYPE* CaptureInputFocus)();
    void(OPENVR_FNTABLE_CALLTYPE* ReleaseInputFocus)();
    bool(OPENVR_FNTABLE_CALLTYPE* IsInputFocusCapturedByAnotherProcess)();
    uint32_t(OPENVR_FNTABLE_CALLTYPE* DriverDebugRequest)(TrackedDeviceIndex_t, char*, char*, uint32_t);
    EVRFirmwareError(OPENVR_FNTABLE_CALLTYPE* PerformFirmwareUpdate)(TrackedDeviceIndex_t);
    void(OPENVR_FNTABLE_CALLTYPE* AcknowledgeQuit_Exiting)();
    void(OPENVR_FNTABLE_CALLTYPE* AcknowledgeQuit_UserPrompt)();
};

// IVRCompositor 015 layout (1.0.0/1.0.1) - differs from 016 at index 30+
struct VR_IVRCompositor015_FnTable {
    void(OPENVR_FNTABLE_CALLTYPE* SetTrackingSpace)(ETrackingUniverseOrigin);
    ETrackingUniverseOrigin(OPENVR_FNTABLE_CALLTYPE* GetTrackingSpace)();
    EVRCompositorError(OPENVR_FNTABLE_CALLTYPE* WaitGetPoses)(struct TrackedDevicePose_t*, uint32_t,
                                                              struct TrackedDevicePose_t*, uint32_t);
    EVRCompositorError(OPENVR_FNTABLE_CALLTYPE* GetLastPoses)(struct TrackedDevicePose_t*, uint32_t,
                                                              struct TrackedDevicePose_t*, uint32_t);
    EVRCompositorError(OPENVR_FNTABLE_CALLTYPE* GetLastPoseForTrackedDeviceIndex)(TrackedDeviceIndex_t,
                                                                                  struct TrackedDevicePose_t*,
                                                                                  struct TrackedDevicePose_t*);
    EVRCompositorError(OPENVR_FNTABLE_CALLTYPE* Submit)(EVREye, struct Texture_t*, struct VRTextureBounds_t*,
                                                        EVRSubmitFlags);
    void(OPENVR_FNTABLE_CALLTYPE* ClearLastSubmittedFrame)();
    void(OPENVR_FNTABLE_CALLTYPE* PostPresentHandoff)();
    bool(OPENVR_FNTABLE_CALLTYPE* GetFrameTiming)(struct Compositor_FrameTiming*, uint32_t);
    float(OPENVR_FNTABLE_CALLTYPE* GetFrameTimeRemaining)();
    void(OPENVR_FNTABLE_CALLTYPE* GetCumulativeStats)(struct Compositor_CumulativeStats*, uint32_t);
    void(OPENVR_FNTABLE_CALLTYPE* FadeToColor)(float, float, float, float, float, bool);
    void(OPENVR_FNTABLE_CALLTYPE* FadeGrid)(float, bool);
    EVRCompositorError(OPENVR_FNTABLE_CALLTYPE* SetSkyboxOverride)(struct Texture_t*, uint32_t);
    void(OPENVR_FNTABLE_CALLTYPE* ClearSkyboxOverride)();
    void(OPENVR_FNTABLE_CALLTYPE* CompositorBringToFront)();
    void(OPENVR_FNTABLE_CALLTYPE* CompositorGoToBack)();
    void(OPENVR_FNTABLE_CALLTYPE* CompositorQuit)();
    bool(OPENVR_FNTABLE_CALLTYPE* IsFullscreen)();
    uint32_t(OPENVR_FNTABLE_CALLTYPE* GetCurrentSceneFocusProcess)();
    uint32_t(OPENVR_FNTABLE_CALLTYPE* GetLastFrameRenderer)();
    bool(OPENVR_FNTABLE_CALLTYPE* CanRenderScene)();
    void(OPENVR_FNTABLE_CALLTYPE* ShowMirrorWindow)();
    void(OPENVR_FNTABLE_CALLTYPE* HideMirrorWindow)();
    bool(OPENVR_FNTABLE_CALLTYPE* IsMirrorWindowVisible)();
    void(OPENVR_FNTABLE_CALLTYPE* CompositorDumpImages)();
    bool(OPENVR_FNTABLE_CALLTYPE* ShouldAppRenderWithLowResources)();
    void(OPENVR_FNTABLE_CALLTYPE* ForceInterleavedReprojectionOn)(bool);
    void(OPENVR_FNTABLE_CALLTYPE* ForceReconnectProcess)();
    void(OPENVR_FNTABLE_CALLTYPE* SuspendRendering)(bool);
    EVRCompositorError(OPENVR_FNTABLE_CALLTYPE* RequestScreenshot)(EVRScreenshotType, char*, char*);
    EVRScreenshotType(OPENVR_FNTABLE_CALLTYPE* GetCurrentScreenshotType)();
    EVRCompositorError(OPENVR_FNTABLE_CALLTYPE* GetMirrorTextureD3D11)(EVREye, void*, void**);
    EVRCompositorError(OPENVR_FNTABLE_CALLTYPE* GetMirrorTextureGL)(EVREye, glUInt_t*, glSharedTextureHandle_t*);
    bool(OPENVR_FNTABLE_CALLTYPE* ReleaseSharedGLTexture)(glUInt_t, glSharedTextureHandle_t);
    void(OPENVR_FNTABLE_CALLTYPE* LockGLSharedTextureForAccess)(glSharedTextureHandle_t);
    void(OPENVR_FNTABLE_CALLTYPE* UnlockGLSharedTextureForAccess)(glSharedTextureHandle_t);
};

static VR_IVRSystem_FnTable g_sys012{};
static VR_IVRSystem015_FnTable g_sys015{};
static VR_IVRCompositor015_FnTable g_comp015{};
static VR_IVRCompositor_FnTable g_comp016{};
static VR_IVRApplications_FnTable g_apps{};
static VR_IVRRenderModels_FnTable g_models{};
static VR_IVRSettings_FnTable g_settings{};
static VR_IVRChaperone_FnTable g_chap{};
static VR_IVRChaperoneSetup_FnTable g_chapSetup{};
static VR_IVROverlay_FnTable g_overlay{};
static std::once_flag g_initTablesOnce;


// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static void SetErr(ETrackedPropertyError* e, ETrackedPropertyError v) {
    if (e) *e = v;
}

static void FillPose(struct TrackedDevicePose_t& p, bool connected, bool valid, const HmdMatrix34_t& mat) {
    memset(&p, 0, sizeof(p));
    p.mDeviceToAbsoluteTracking = mat;
    p.eTrackingResult = valid ? TrackingResult_Running_OK : TrackingResult_Uninitialized;
    p.bPoseIsValid = valid;
    p.bDeviceIsConnected = connected;
}

static HmdMatrix34_t Identity34() {
    HmdMatrix34_t m{};
    m.m[0][0] = m.m[1][1] = m.m[2][2] = 1.0f;
    return m;
}

static HmdMatrix34_t HeadPose() {
    HmdMatrix34_t m = Identity34();
    if (g_cfg.tracking == "mouse") {
        float yaw = (float)(g_yawDeg * M_PI / 180.0);
        float pitch = (float)(g_pitchDeg * M_PI / 180.0);
        float cy = cosf(yaw), sy = sinf(yaw);
        float cp = cosf(pitch), sp = sinf(pitch);
        m.m[0][0] = cy;
        m.m[0][1] = sy * sp;
        m.m[0][2] = sy * cp;
        m.m[1][0] = 0.0f;
        m.m[1][1] = cp;
        m.m[1][2] = -sp;
        m.m[2][0] = -sy;
        m.m[2][1] = cy * sp;
        m.m[2][2] = cy * cp;
    }
    return m;
}

static void UpdateMousePose() {
    if (g_cfg.tracking != "mouse") return;
    POINT p;
    if (!GetCursorPos(&p)) return;
    if (g_mouseValid) {
        float dx = (float)(p.x - g_lastMouse.x);
        float dy = (float)(p.y - g_lastMouse.y);
        if (dx != 0.0f || dy != 0.0f) {
            g_yawDeg -= dx * g_cfg.mouseSensitivity;
            g_pitchDeg += dy * g_cfg.mouseSensitivity * (g_cfg.invertY ? -1.0f : 1.0f);
            if (g_pitchDeg > 85.0f) g_pitchDeg = 85.0f;
            if (g_pitchDeg < -85.0f) g_pitchDeg = -85.0f;
            while (g_yawDeg > 180.0f) g_yawDeg -= 360.0f;
            while (g_yawDeg < -180.0f) g_yawDeg += 360.0f;
            g_lastMouse = p;
        }
    } else {
        g_lastMouse = p;
        g_mouseValid = true;
    }
}

// 3D Vision / geo-11 hotkeys, polled once per frame while the game (or our
// presentation window) has focus:
//   Ctrl+F3 / Ctrl+F4  separation down / up      Ctrl+F5 / Ctrl+F6  convergence down / up
//   Ctrl+F9 / Ctrl+F10 crosstalk down / up
//   Ctrl+F7            save to rez3d.ini      Ctrl+T             toggle stereo
static void ShowStereoOsd(const char* suffix = "") {
    char text[128];
    if (g_stereoOn)
        sprintf_s(text, "SEP %.0f%%   CONV %.1f   XT %.3f%s", g_cfg.separation, g_cfg.convergence, g_cfg.crosstalk,
                  suffix);
    else
        sprintf_s(text, "STEREO OFF%s", suffix);
    Presenter::Get().ShowOsd(text);
}

static void PollHotkeys() {
    static bool shownAtStart = false;
    if (!shownAtStart) {
        shownAtStart = true;
        ShowStereoOsd();
    }

    static LARGE_INTEGER last{};
    static bool wasT = false, wasF7 = false, adjusting = false;
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    float dt = last.QuadPart ? (float)((double)(now.QuadPart - last.QuadPart) / (double)g_qpcFreq.QuadPart) : 0.0f;
    last = now;
    if (dt > 0.1f) dt = 0.1f;

    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    if (pid != GetCurrentProcessId() || !ctrl) {
        wasT = wasF7 = false;
        if (adjusting) {
            adjusting = false;
            LogPrintf("Stereo: separation=%.1f%% convergence=%.3f", g_cfg.separation, g_cfg.convergence);
        }
        return;
    }
    auto down = [](int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; };

    // Rates per second while held; convergence scales so it is usable at any depth.
    bool changed = false;
    if (down(VK_F3)) { g_cfg.separation -= 20.0f * dt; changed = true; }
    if (down(VK_F4)) { g_cfg.separation += 20.0f * dt; changed = true; }
    if (down(VK_F5)) { g_cfg.convergence *= powf(1.0f / 1.6f, dt); changed = true; }
    if (down(VK_F6)) { g_cfg.convergence *= powf(1.6f, dt); changed = true; }
    if (down(VK_F9)) { g_cfg.crosstalk -= 0.05f * dt; changed = true; }
    if (down(VK_F10)) { g_cfg.crosstalk += 0.05f * dt; changed = true; }
    if (g_cfg.crosstalk < 0.0f) g_cfg.crosstalk = 0.0f;
    if (g_cfg.crosstalk > 0.3f) g_cfg.crosstalk = 0.3f;
    if (g_cfg.separation < 0.0f) g_cfg.separation = 0.0f;
    if (g_cfg.separation > 200.0f) g_cfg.separation = 200.0f;  // beyond 3D Vision's 100% for depth junkies
    if (g_cfg.convergence < 0.01f) g_cfg.convergence = 0.01f;
    if (g_cfg.convergence > 5000.0f) g_cfg.convergence = 5000.0f;
    if (changed) {
        adjusting = true;
        Presenter::Get().SetCrosstalk(g_cfg.crosstalk);
        ShowStereoOsd();
    } else if (adjusting) {
        adjusting = false;
        LogPrintf("Stereo: separation=%.1f%% convergence=%.3f", g_cfg.separation, g_cfg.convergence);
    }

    bool t = down('T');
    if (t && !wasT) {
        g_stereoOn = !g_stereoOn;
        LogPrintf("Stereo: %s", g_stereoOn ? "on" : "off");
        ShowStereoOsd();
    }
    wasT = t;

    bool f7 = down(VK_F7);
    if (f7 && !wasF7) {
        bool ok = SaveStereoSettings(g_dllDir, g_cfg.separation, g_cfg.convergence, g_cfg.crosstalk);
        LogPrintf("Stereo: saved separation=%.1f%% convergence=%.3f (%s)", g_cfg.separation, g_cfg.convergence,
                  ok ? "ok" : "failed");
        ShowStereoOsd(ok ? "   SAVED" : "   SAVE FAILED");
    }
    wasF7 = f7;
}

static void LogFrameSignature() {
    std::string sig;
    int n = g_siteNext.load();
    for (int i = 0; i < n && i < kMaxSites; i++) {
        unsigned c = g_siteCount[i].exchange(0);
        if (c) {
            char b[96];
            sprintf_s(b, "%s=%u ", g_siteName[i] + (strncmp(g_siteName[i], "Sys_", 4) == 0 ? 4 : 0), c);
            sig += b;
        }
    }
    char nf[48];
    sprintf_s(nf, "near=%.3f far=%.0f", g_lastNear, g_lastFar);
    sig += nf;
    static std::string last;
    static unsigned logged = 0;
    if (g_cfg.logFrameCalls >= 2 && sig != last && logged < 400) {
        logged++;
        LogPrintf("FrameSig #%u: %s", g_waitCount, sig.c_str());
    }
    last = sig;
}

static void FillPoses(struct TrackedDevicePose_t* poses, uint32_t count) {
    if (!poses) return;
    HmdMatrix34_t head = HeadPose();
    for (uint32_t i = 0; i < count; i++) {
        if (i == 0)
            FillPose(poses[i], true, true, head);
        else
            FillPose(poses[i], false, false, Identity34());
    }
}

static void InitRuntimeConfig() {
    if (g_initialized) return;

    GetModuleFileNameA(g_hModule, g_dllDir, MAX_PATH);
    char* slash = strrchr(g_dllDir, '\\');
    if (slash) *slash = 0;

    LoadConfig(g_dllDir, g_cfg);
    g_focusEventType = (uint32_t)g_cfg.focusEvent;
    {
        char ev[32] = {};
        if (GetEnvironmentVariableA("REZ3D_EVENT", ev, sizeof(ev)) > 0) g_focusEventType = (uint32_t)atoi(ev);
    }

    {
        char hl[8] = {};
        if (GetEnvironmentVariableA("REZ3D_HEADLESS", hl, sizeof(hl)) > 0 && hl[0] != '0') g_cfg.headless = 1;
    }

    int outW = 1920, outH = 1080;
    RECT rect{};
    GetOutputSize(g_cfg, outW, outH, rect);
    int ew = 0, eh = 0;
    ComputeEyeSize(g_cfg, outW, outH, ew, eh);
    g_eyeW = (uint32_t)ew;
    g_eyeH = (uint32_t)eh;

    float halfH = g_cfg.fovH * 0.5f * (float)M_PI / 180.0f;
    g_tanH = tanf(halfH);
    g_tanV = g_tanH * (float)outH / (float)outW;

    if (g_cfg.fps <= 0) {
        int hz = GetMonitorRefreshHz(g_cfg);
        g_cfg.fps = hz > 0 ? hz : 60;
    }

    int widthMM = g_cfg.screenWidthMM > 0 ? g_cfg.screenWidthMM : GetMonitorWidthMM(g_cfg);
    g_screenWidthMM = widthMM >= 100 ? (float)widthMM : 600.0f;
    LogPrintf("Screen width: %.0f mm (%s)", g_screenWidthMM,
              g_cfg.screenWidthMM > 0 ? "ini" : (widthMM >= 100 ? "EDID" : "EDID unavailable, assumed"));

    QueryPerformanceFrequency(&g_qpcFreq);
    QueryPerformanceCounter(&g_lastVsync);
    g_lastWait = g_lastVsync;

    char envNoPres[8] = {};
    g_noPresenter = GetEnvironmentVariableA("REZ3D_NO_PRESENTER", envNoPres, sizeof(envNoPres)) > 0;
    g_initialized = true;
    LogPrintf("=== Rez3D shim init (presenter=%d) ===", g_noPresenter ? 0 : 1);
    LogPrintf("DLL dir: %s", g_dllDir);
    LogPrintf("Config: format=%s monitor=%d fullscreen=%d vsync=%d separation=%.1f%% convergence=%.3f fovH=%.1f "
              "renderScale=%.2f tracking=%s fps=%d pacing=%d gpu=%d",
              g_cfg.format.c_str(), g_cfg.monitor, g_cfg.fullscreen, g_cfg.vsync, g_cfg.separation, g_cfg.convergence,
              g_cfg.fovH, g_cfg.renderScale, g_cfg.tracking.c_str(), g_cfg.fps, g_cfg.framePacing,
              g_cfg.gpuComposite);
    LogPrintf("Eye render target: %ux%u (output %dx%d), tanH=%.4f tanV=%.4f", g_eyeW, g_eyeH, outW, outH, g_tanH,
              g_tanV);

    Presenter::Get().Configure(g_cfg);
    // Test runs stay silent; normal launches undo any mute left from the mixer or a test
    char muteEnv[8] = {};
    bool testRun = GetEnvironmentVariableA("REZ3D_MUTE", muteEnv, sizeof(muteEnv)) > 0 && muteEnv[0] != '0';
    if (g_cfg.headless || testRun)
        StartGameAudioMute(true);
    else if (g_cfg.unmuteOnStart)
        StartGameAudioMute(false);
    QueueFocusEvents();
}

// ---------------------------------------------------------------------------
// IVRSystem (015 signatures)
// ---------------------------------------------------------------------------
static void __stdcall Sys_GetRecommendedRenderTargetSize(uint32_t* w, uint32_t* h) {
    LogPrintf("sys.GetRecommendedRenderTargetSize");
    if (w) *w = g_eyeW;
    if (h) *h = g_eyeH;
}

// ---------------------------------------------------------------------------
// Stereo model (NVIDIA 3D Vision / geo-11 semantics)
//
// 3D Vision shifts each eye in clip space: x' = x + sep * (w - conv). In NDC
// that is x/w + sep * (1 - conv/w): parallax is 0 at w = conv and tends to sep
// at infinity, so separation alone fixes the on-screen parallax of distant
// objects and convergence picks which depth sits on the screen. The same
// result comes from cameras offset by +-sep*conv*tanH with frustums shifted
// by sep*tanH, which is what we report through OpenVR.
//
// sep (total NDC parallax at infinity) = Separation% * 65 mm / screen width,
// so 100% means distant objects are a real eye distance apart on the screen.
// ---------------------------------------------------------------------------
static const float kEyeDistanceMM = 65.0f;
static float SeparationNdc() {
    if (!g_stereoOn) return 0.0f;
    // Menus get their own (lower) separation; the presenter detects them
    return Presenter::Get().EffectiveSeparation(g_cfg.separation) * 0.01f * kEyeDistanceMM / g_screenWidthMM;
}

static float HalfEyeOffsetMeters() {
    return SeparationNdc() * g_cfg.convergence * g_tanH;
}

// Frustum tangents in OpenVR's convention (left/top negative, y pointing down).
static void EyeTangents(EVREye eye, float& l, float& r, float& t, float& b) {
    float shift = SeparationNdc() * g_tanH;
    if (eye != Eye_Left) shift = -shift;
    l = -g_tanH + shift;
    r = g_tanH + shift;
    t = -g_tanV;
    b = g_tanV;
}

// Same layout as OpenVR's own ComposeProjection (row-major, column vectors).
static HmdMatrix44_t ProjectionMatrixImpl(EVREye eye, float nearZ, float farZ, bool d3dDepth) {
    HmdMatrix44_t m{};
    float l, r, t, b;
    EyeTangents(eye, l, r, t, b);
    float idx = 1.0f / (r - l);
    float idy = 1.0f / (b - t);
    m.m[0][0] = 2.0f * idx;
    m.m[0][2] = (r + l) * idx;
    m.m[1][1] = 2.0f * idy;
    m.m[1][2] = (b + t) * idy;
    if (d3dDepth) {
        m.m[2][2] = farZ / (nearZ - farZ);
        m.m[2][3] = (nearZ * farZ) / (nearZ - farZ);
    } else {
        m.m[2][2] = (farZ + nearZ) / (nearZ - farZ);
        m.m[2][3] = (2.0f * nearZ * farZ) / (nearZ - farZ);
    }
    m.m[3][2] = -1.0f;
    m.m[3][3] = 0.0f;
    return m;
}

static HmdMatrix44_t __stdcall Sys_GetProjectionMatrix015(EVREye eye, float nearZ, float farZ) {
    TRACE_FN("sys.GetProjectionMatrix015 eye=%d near=%.3f far=%.1f", (int)eye, nearZ, farZ);
    g_lastNear = nearZ;
    g_lastFar = farZ;
    return ProjectionMatrixImpl(eye, nearZ, farZ, true);
}

static HmdMatrix44_t __stdcall Sys_GetProjectionMatrix012(EVREye eye, float nearZ, float farZ,
                                                          EGraphicsAPIConvention conv) {
    return ProjectionMatrixImpl(eye, nearZ, farZ, conv == API_DirectX);
}

static void __stdcall Sys_GetProjectionRaw(EVREye eye, float* l, float* r, float* t, float* b) {
    TRACE_FN("sys.GetProjectionRaw eye=%d", (int)eye);
    float el, er, et, eb;
    EyeTangents(eye, el, er, et, eb);
    if (l) *l = el;
    if (r) *r = er;
    if (t) *t = et;
    if (b) *b = eb;
}

static bool __stdcall Sys_ComputeDistortion015(EVREye, float u, float v, DistortionCoordinates_t* out) {
    if (out) {
        out->rfRed[0] = out->rfGreen[0] = out->rfBlue[0] = u;
        out->rfRed[1] = out->rfGreen[1] = out->rfBlue[1] = v;
    }
    return true;
}

static DistortionCoordinates_t __stdcall Sys_ComputeDistortion012(EVREye, float u, float v) {
    DistortionCoordinates_t d{};
    d.rfRed[0] = d.rfGreen[0] = d.rfBlue[0] = u;
    d.rfRed[1] = d.rfGreen[1] = d.rfBlue[1] = v;
    return d;
}

static HmdMatrix34_t __stdcall Sys_GetEyeToHeadTransform(EVREye eye) {
    TRACE_FN("sys.GetEyeToHeadTransform eye=%d", (int)eye);
    HmdMatrix34_t m = Identity34();
    float halfIpd = HalfEyeOffsetMeters();
    m.m[0][3] = (eye == Eye_Left) ? -halfIpd : halfIpd;
    return m;
}

static bool __stdcall Sys_GetTimeSinceLastVsync(float* sec, uint64_t* frame) {
    TRACE_FN("sys.GetTimeSinceLastVsync");
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    if (sec) *sec = (float)((double)(now.QuadPart - g_lastVsync.QuadPart) / (double)g_qpcFreq.QuadPart);
    if (frame) *frame = g_frameCounter;
    return true;
}

static int32_t __stdcall Sys_GetD3D9AdapterIndex() { return 0; }

static void __stdcall Sys_GetDXGIOutputInfo(int32_t* index) {
    LogPrintf("sys.GetDXGIOutputInfo");
    if (index) *index = 0;
}

static bool __stdcall Sys_IsDisplayOnDesktop() { LogPrintf("sys.IsDisplayOnDesktop"); return false; }
static bool __stdcall Sys_SetDisplayVisibility(bool) { return true; }

static void __stdcall Sys_GetDeviceToAbsoluteTrackingPose(ETrackingUniverseOrigin origin, float pred,
                                                          TrackedDevicePose_t* poses, uint32_t count) {
    TRACE_FN("sys.GetDeviceToAbsoluteTrackingPose count=%u", count);
    FillPoses(poses, count);
}

static void __stdcall Sys_ResetSeatedZeroPose() {}
static HmdMatrix34_t __stdcall Sys_GetSeatedZeroPoseToStandingAbsoluteTrackingPose() { return Identity34(); }
static HmdMatrix34_t __stdcall Sys_GetRawZeroPoseToStandingAbsoluteTrackingPose() { return Identity34(); }

static uint32_t __stdcall Sys_GetSortedTrackedDeviceIndicesOfClass(ETrackedDeviceClass cls,
                                                                    TrackedDeviceIndex_t* arr, uint32_t count,
                                                                    TrackedDeviceIndex_t) {
    if (cls == TrackedDeviceClass_HMD && arr && count > 0) {
        arr[0] = k_unTrackedDeviceIndex_Hmd;
        return 1;
    }
    return 0;
}

static EDeviceActivityLevel __stdcall Sys_GetTrackedDeviceActivityLevel(TrackedDeviceIndex_t idx) {
    return idx == 0 ? k_EDeviceActivityLevel_UserInteraction : k_EDeviceActivityLevel_Unknown;
}

static void __stdcall Sys_ApplyTransform(TrackedDevicePose_t* out, TrackedDevicePose_t* pose, HmdMatrix34_t*) {
    if (out && pose) *out = *pose;
}

static TrackedDeviceIndex_t __stdcall Sys_GetTrackedDeviceIndexForControllerRole(ETrackedControllerRole) {
    return k_unTrackedDeviceIndexInvalid;
}
static ETrackedControllerRole __stdcall Sys_GetControllerRoleForTrackedDeviceIndex(TrackedDeviceIndex_t) {
    return TrackedControllerRole_Invalid;
}
static ETrackedDeviceClass __stdcall Sys_GetTrackedDeviceClass(TrackedDeviceIndex_t idx) {
    TRACE_FN("sys.GetTrackedDeviceClass idx=%u", idx);
    return idx == 0 ? TrackedDeviceClass_HMD : TrackedDeviceClass_Invalid;
}
static bool __stdcall Sys_IsTrackedDeviceConnected(TrackedDeviceIndex_t idx) { TRACE_FN("sys.IsTrackedDeviceConnected idx=%u", idx); return idx == 0; }

static bool __stdcall Sys_GetBoolTrackedDeviceProperty(TrackedDeviceIndex_t idx, ETrackedDeviceProperty prop,
                                                        ETrackedPropertyError* err) {
    LogPrintf("sys.GetBoolProp idx=%u prop=%d", idx, (int)prop);
    switch (prop) {
    case Prop_DeviceIsWireless_Bool:
    case Prop_DeviceIsCharging_Bool:
    case Prop_ContainsProximitySensor_Bool:
    case Prop_DeviceProvidesBatteryStatus_Bool:
    case Prop_DeviceCanPowerOff_Bool:
    case Prop_HasCamera_Bool:
    case Prop_IsOnDesktop_Bool:
    case Prop_ReportsTimeSinceVSync_Bool:
        SetErr(err, TrackedProp_Success);
        return false;
    default:
        SetErr(err, TrackedProp_UnknownProperty);
        return false;
    }
}

static float __stdcall Sys_GetFloatTrackedDeviceProperty(TrackedDeviceIndex_t idx, ETrackedDeviceProperty prop,
                                                          ETrackedPropertyError* err) {
    LogPrintf("sys.GetFloatProp idx=%u prop=%d", idx, (int)prop);
    switch (prop) {
    case Prop_DisplayFrequency_Float:
        SetErr(err, TrackedProp_Success);
        return (float)g_cfg.fps;
    case Prop_UserIpdMeters_Float:
        SetErr(err, TrackedProp_Success);
        return 2.0f * HalfEyeOffsetMeters();
    case Prop_DeviceBatteryPercentage_Float:
        SetErr(err, TrackedProp_Success);
        return 1.0f;
    case Prop_SecondsFromVsyncToPhotons_Float:
        SetErr(err, TrackedProp_Success);
        return 0.011f;
    case Prop_FieldOfViewLeftDegrees_Float:
    case Prop_FieldOfViewRightDegrees_Float:
        SetErr(err, TrackedProp_Success);
        return (float)(atan(g_tanH) * 180.0 / M_PI);
    case Prop_FieldOfViewTopDegrees_Float:
    case Prop_FieldOfViewBottomDegrees_Float:
        SetErr(err, TrackedProp_Success);
        return (float)(atan(g_tanV) * 180.0 / M_PI);
    default:
        SetErr(err, TrackedProp_UnknownProperty);
        return 0.0f;
    }
}

static int32_t __stdcall Sys_GetInt32TrackedDeviceProperty(TrackedDeviceIndex_t idx, ETrackedDeviceProperty prop,
                                                            ETrackedPropertyError* err) {
    LogPrintf("sys.GetInt32Prop idx=%u prop=%d", idx, (int)prop);
    switch (prop) {
    case Prop_DeviceClass_Int32:
        SetErr(err, TrackedProp_Success);
        return (int32_t)TrackedDeviceClass_HMD;
    case Prop_ControllerRoleHint_Int32:
        SetErr(err, TrackedProp_Success);
        return (int32_t)TrackedControllerRole_Invalid;
    case Prop_Axis0Type_Int32:
    case Prop_Axis1Type_Int32:
    case Prop_Axis2Type_Int32:
    case Prop_Axis3Type_Int32:
    case Prop_Axis4Type_Int32:
        SetErr(err, TrackedProp_Success);
        return 0;
    default:
        SetErr(err, TrackedProp_UnknownProperty);
        return 0;
    }
}

static uint64_t __stdcall Sys_GetUint64TrackedDeviceProperty(TrackedDeviceIndex_t idx, ETrackedDeviceProperty prop,
                                                              ETrackedPropertyError* err) {
    LogPrintf("sys.GetUint64Prop idx=%u prop=%d", idx, (int)prop);
    switch (prop) {
    case Prop_SupportedButtons_Uint64:
    case Prop_HardwareRevision_Uint64:
    case Prop_FirmwareVersion_Uint64:
    case Prop_FPGAVersion_Uint64:
    case Prop_VRCVersion_Uint64:
    case Prop_RadioVersion_Uint64:
    case Prop_DongleVersion_Uint64:
    case Prop_DisplayFirmwareVersion_Uint64:
        SetErr(err, TrackedProp_Success);
        return 0;
    default:
        SetErr(err, TrackedProp_UnknownProperty);
        return 0;
    }
}

static HmdMatrix34_t __stdcall Sys_GetMatrix34TrackedDeviceProperty(TrackedDeviceIndex_t,
                                                                    ETrackedDeviceProperty prop,
                                                                    ETrackedPropertyError* err) {
    if (prop == Prop_StatusDisplayTransform_Matrix34) {
        SetErr(err, TrackedProp_Success);
        return Identity34();
    }
    SetErr(err, TrackedProp_UnknownProperty);
    return Identity34();
}

static const char* PropString(ETrackedDeviceProperty prop) {
    switch (prop) {
    case Prop_TrackingSystemName_String:
    case Prop_RenderModelName_String:
        return "rez3d";
    case Prop_ModelNumber_String:
        return "Rez Infinite 3D";
    case Prop_SerialNumber_String:
        return "REZ3D0001";
    case Prop_ManufacturerName_String:
        return "Rez3D";
    case Prop_TrackingFirmwareVersion_String:
    case Prop_HardwareRevision_String:
    case Prop_DriverVersion_String:
        return "1.0";
    case Prop_AttachedDeviceId_String:
    case Prop_AllWirelessDongleDescriptions_String:
    case Prop_ConnectedWirelessDongle_String:
    case Prop_Firmware_ManualUpdateURL_String:
        return "";
    default:
        return nullptr;
    }
}

static uint32_t __stdcall Sys_GetStringTrackedDeviceProperty(TrackedDeviceIndex_t idx, ETrackedDeviceProperty prop,
                                                              char* value, uint32_t size,
                                                              ETrackedPropertyError* err) {
    LogPrintf("sys.GetStringProp idx=%u prop=%d size=%u", idx, (int)prop, size);
    const char* s = PropString(prop);
    if (!s) {
        SetErr(err, TrackedProp_UnknownProperty);
        if (value && size) value[0] = 0;
        return 0;
    }
    SetErr(err, TrackedProp_Success);
    uint32_t len = (uint32_t)strlen(s) + 1;
    if (value && size) {
        uint32_t n = len < size ? len : size;
        memcpy(value, s, n - 1);
        value[n - 1] = 0;
        return n - 1;
    }
    return len - 1;
}

static char* __stdcall Sys_GetPropErrorNameFromEnum(ETrackedPropertyError) { return (char*)""; }
static bool __stdcall Sys_PollNextEvent(VREvent_t* ev, uint32_t cb) {
    bool have = PopEvent(ev, cb);
    TRACE_FN("sys.PollNextEvent cb=%u -> %d", cb, have ? 1 : 0);
    return have;
}
static bool __stdcall Sys_PollNextEventWithPose(ETrackingUniverseOrigin, VREvent_t* ev, uint32_t cb,
                                                TrackedDevicePose_t* pose) {
    bool have = PopEvent(ev, cb);
    if (have && pose) FillPose(*pose, true, true, HeadPose());
    TRACE_FN("sys.PollNextEventWithPose cb=%u -> %d", cb, have ? 1 : 0);
    return have;
}
static char* __stdcall Sys_GetEventTypeNameFromEnum(EVREventType) { return (char*)""; }

static HiddenAreaMesh_t __stdcall Sys_GetHiddenAreaMesh015(EVREye eye, EHiddenAreaMeshType type) {
    LogPrintf("sys.GetHiddenAreaMesh015 eye=%d type=%d", (int)eye, (int)type);
    HiddenAreaMesh_t m{};
    m.pVertexData = nullptr;
    m.unTriangleCount = 0;
    (void)type;
    return m;
}

static HiddenAreaMesh_t __stdcall Sys_GetHiddenAreaMesh012(EVREye) {
    HiddenAreaMesh_t m{};
    m.pVertexData = nullptr;
    m.unTriangleCount = 0;
    return m;
}

static bool __stdcall Sys_GetControllerState015(TrackedDeviceIndex_t idx, VRControllerState_t*, uint32_t) {
    TRACE_FN("sys.GetControllerState015 idx=%u", idx);
    return false;
}
static bool __stdcall Sys_GetControllerState012(TrackedDeviceIndex_t, VRControllerState_t*) { return false; }
static bool __stdcall Sys_GetControllerStateWithPose015(ETrackingUniverseOrigin, TrackedDeviceIndex_t,
                                                        VRControllerState_t*, uint32_t, TrackedDevicePose_t*) {
    return false;
}
static bool __stdcall Sys_GetControllerStateWithPose012(ETrackingUniverseOrigin, TrackedDeviceIndex_t,
                                                        VRControllerState_t*, TrackedDevicePose_t*) {
    return false;
}

static void __stdcall Sys_TriggerHapticPulse(TrackedDeviceIndex_t, uint32_t, unsigned short) {}
static char* __stdcall Sys_GetButtonIdNameFromEnum(EVRButtonId) { return (char*)""; }
static char* __stdcall Sys_GetControllerAxisTypeNameFromEnum(EVRControllerAxisType) { return (char*)""; }
static bool __stdcall Sys_CaptureInputFocus() {
    LogPrintf("sys.CaptureInputFocus");
    QueueFocusEvents();
    return true;
}
static void __stdcall Sys_ReleaseInputFocus() {
    PushEvent(VREvent_InputFocusReleased, 0, GetCurrentProcessId(), false);
}
static bool __stdcall Sys_IsInputFocusCapturedByAnotherProcess() { TRACE_FN("sys.IsInputFocusCapturedByAnotherProcess"); return false; }
static uint32_t __stdcall Sys_DriverDebugRequest(TrackedDeviceIndex_t, char*, char*, uint32_t) { return 0; }
static EVRFirmwareError __stdcall Sys_PerformFirmwareUpdate(TrackedDeviceIndex_t) { return VRFirmwareError_None; }
static void __stdcall Sys_AcknowledgeQuit_Exiting() {}
static void __stdcall Sys_AcknowledgeQuit_UserPrompt() {}

// ---------------------------------------------------------------------------
// IVRCompositor
// ---------------------------------------------------------------------------
static void CopyBounds(const VRTextureBounds_t* src, EyeBounds& dst) {
    if (src) {
        dst.uMin = src->uMin;
        dst.vMin = src->vMin;
        dst.uMax = src->uMax;
        dst.vMax = src->vMax;
    } else {
        dst = EyeBounds{};
    }
}

static EVRCompositorError __stdcall Comp_Submit(EVREye eye, Texture_t* texture, VRTextureBounds_t* bounds,
                                                EVRSubmitFlags flags) {
    g_submitCount++;
    if (eye == Eye_Right) g_frameCounter++;

    if (!texture || !texture->handle) {
        if (g_submitCount < 10)
            LogPrintf("Submit: eye=%d texture=%p (null)", (int)eye, texture ? texture->handle : nullptr);
        return VRCompositorError_InvalidTexture;
    }

    if (g_submitCount <= 10 || (g_cfg.logFrameCalls && g_submitCount - g_lastSubmitLog >= 300)) {
        g_lastSubmitLog = g_submitCount;
        LogPrintf("Submit #%u: eye=%d handle=%p eType=%d eColorSpace=%d bounds=%s flags=0x%X", g_submitCount,
                  (int)eye, texture->handle, (int)texture->eType, (int)texture->eColorSpace,
                  bounds ? "yes" : "no", (unsigned)flags);
    }

    if (texture->eType != API_DirectX) {
        if (!g_loggedTexture) {
            LogPrintf("Submit: unsupported texture type %d (only DirectX/D3D11 supported)", (int)texture->eType);
            g_loggedTexture = true;
        }
        return VRCompositorError_InvalidTexture;
    }

    // The texture handle is an ID3D11Texture2D*
    if (g_noPresenter) {
        LogPrintf("Submit (no presenter): eye=%d tex=%p", (int)eye, texture->handle);
        return VRCompositorError_None;
    }
    ID3D11Texture2D* tex = reinterpret_cast<ID3D11Texture2D*>(texture->handle);
    EyeBounds b;
    CopyBounds(bounds, b);
    Presenter::Get().SubmitEye(eye == Eye_Right ? 1 : 0, tex, b, (int)flags);
    return VRCompositorError_None;
}

static EVRCompositorError __stdcall Comp_WaitGetPoses(TrackedDevicePose_t* renderPoses,
                                                      uint32_t renderCount, TrackedDevicePose_t* gamePoses,
                                                      uint32_t gameCount) {
    g_waitCount++;
    // Like a real compositor, hold the game until its previous frame is on
    // screen; otherwise it renders hundreds of frames per second for nothing.
    if (g_cfg.framePacing && !g_noPresenter) {
        unsigned timeoutMs = (unsigned)(2000 / (g_cfg.fps > 0 ? g_cfg.fps : 60));
        Presenter::Get().WaitForPresent(timeoutMs < 8 ? 8 : (timeoutMs > 50 ? 50 : timeoutMs));
    }
    UpdateMousePose();
    PollHotkeys();
    LogFrameSignature();
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    g_lastWait = now;
    g_lastVsync = now;
    FillPoses(renderPoses, renderCount);
    FillPoses(gamePoses, gameCount);
    if (g_waitCount <= 5 || (g_cfg.logFrameCalls && (g_waitCount % 300) == 0)) {
        LogPrintf("WaitGetPoses #%u: renderCount=%u gameCount=%u (mouse yaw=%.1f pitch=%.1f)", g_waitCount,
                  renderCount, gameCount, g_yawDeg, g_pitchDeg);
    }
    return VRCompositorError_None;
}

static EVRCompositorError __stdcall Comp_GetLastPoses(TrackedDevicePose_t* renderPoses, uint32_t renderCount,
                                                      TrackedDevicePose_t* gamePoses, uint32_t gameCount) {
    FillPoses(renderPoses, renderCount);
    FillPoses(gamePoses, gameCount);
    return VRCompositorError_None;
}

static EVRCompositorError __stdcall Comp_GetLastPoseForTrackedDeviceIndex(TrackedDeviceIndex_t index,
                                                                          TrackedDevicePose_t* outputPose,
                                                                          TrackedDevicePose_t* outputGamePose) {
    if (index != 0 || !outputPose) return VRCompositorError_IndexOutOfRange;
    HmdMatrix34_t head = HeadPose();
    FillPose(*outputPose, true, true, head);
    if (outputGamePose) FillPose(*outputGamePose, true, true, head);
    return VRCompositorError_None;
}

static void __stdcall Comp_SetTrackingSpace(ETrackingUniverseOrigin origin) { g_trackingSpace = (uint32_t)origin; }
static ETrackingUniverseOrigin __stdcall Comp_GetTrackingSpace() {
    return (ETrackingUniverseOrigin)g_trackingSpace;
}
static void __stdcall Comp_ClearLastSubmittedFrame() {}
static void __stdcall Comp_PostPresentHandoff() {}

static bool __stdcall Comp_GetFrameTiming(Compositor_FrameTiming* timing, uint32_t) {
    if (timing) {
        memset(timing, 0, sizeof(*timing));
        timing->m_nSize = sizeof(Compositor_FrameTiming);
        timing->m_flSystemTimeInSeconds = (float)(GetTickCount64() / 1000.0);
    }
    return true;
}

static float __stdcall Comp_GetFrameTimeRemaining() {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    float since = (float)((double)(now.QuadPart - g_lastVsync.QuadPart) / (double)g_qpcFreq.QuadPart);
    float remaining = 1.0f / (float)(g_cfg.fps > 0 ? g_cfg.fps : 60) - since;
    return remaining > 0.0f ? remaining : 0.0f;
}

static void __stdcall Comp_GetCumulativeStats(Compositor_CumulativeStats* stats, uint32_t size) {
    if (stats) memset(stats, 0, size);
}

static void __stdcall Comp_FadeToColor(float, float, float, float, float, bool) {}
static void __stdcall Comp_FadeGrid(float, bool) {}
static EVRCompositorError __stdcall Comp_SetSkyboxOverride(Texture_t*, uint32_t) { return VRCompositorError_None; }
static void __stdcall Comp_ClearSkyboxOverride() {}
static void __stdcall Comp_CompositorBringToFront() {}
static void __stdcall Comp_CompositorGoToBack() {}
static void __stdcall Comp_CompositorQuit() {}
static bool __stdcall Comp_IsFullscreen() { return true; }
static uint32_t __stdcall Comp_GetCurrentSceneFocusProcess() { return GetCurrentProcessId(); }
static uint32_t __stdcall Comp_GetLastFrameRenderer() { return GetCurrentProcessId(); }
static bool __stdcall Comp_CanRenderScene() { return true; }
static void __stdcall Comp_ShowMirrorWindow() {}
static void __stdcall Comp_HideMirrorWindow() {}
static bool __stdcall Comp_IsMirrorWindowVisible() { return false; }
static void __stdcall Comp_CompositorDumpImages() {}
static bool __stdcall Comp_ShouldAppRenderWithLowResources() { return false; }
static void __stdcall Comp_ForceInterleavedReprojectionOn(bool) {}
static void __stdcall Comp_ForceReconnectProcess() {}
static void __stdcall Comp_SuspendRendering(bool suspend) { g_suspended = suspend; }

static EVRCompositorError __stdcall Comp_RequestScreenshot(EVRScreenshotType, char*, char*) {
    return VRCompositorError_None;
}
static EVRScreenshotType __stdcall Comp_GetCurrentScreenshotType() { return VRScreenshotType_None; }

static EVRCompositorError __stdcall Comp_GetMirrorTextureD3D11(EVREye, void*, void**) {
    static bool once = false;
    if (!once) {
        LogPrintf("GetMirrorTextureD3D11 called (not provided; returning RequestFailed)");
        once = true;
    }
    return VRCompositorError_RequestFailed;
}

static EVRCompositorError __stdcall Comp_GetMirrorTextureGL(EVREye, glUInt_t*, glSharedTextureHandle_t*) {
    return VRCompositorError_RequestFailed;
}
static bool __stdcall Comp_ReleaseSharedGLTexture(glUInt_t, glSharedTextureHandle_t) { return false; }
static void __stdcall Comp_LockGLSharedTextureForAccess(glSharedTextureHandle_t) {}
static void __stdcall Comp_UnlockGLSharedTextureForAccess(glSharedTextureHandle_t) {}

// ---------------------------------------------------------------------------
// Stub helpers for optional interfaces
// ---------------------------------------------------------------------------
static uintptr_t StubZero() { return 0; }
static void StubVoid() {}
static float StubFloatZero() { return 0.0f; }
static char* StubEmptyString() { return (char*)""; }

static bool __stdcall Settings_GetBool(char*, char*, bool def, EVRSettingsError* err) {
    if (err) *err = VRSettingsError_None;
    return def;
}
static int32_t __stdcall Settings_GetInt32(char*, char*, int32_t def, EVRSettingsError* err) {
    if (err) *err = VRSettingsError_None;
    return def;
}
static float __stdcall Settings_GetFloat(char*, char*, float def, EVRSettingsError* err) {
    if (err) *err = VRSettingsError_None;
    return def;
}
static void __stdcall Settings_GetString(char*, char*, char* value, uint32_t len, char* def,
                                         EVRSettingsError* err) {
    if (err) *err = VRSettingsError_None;
    if (value && len) {
        if (def) {
            strncpy_s(value, len, def, _TRUNCATE);
        } else {
            value[0] = 0;
        }
    }
}
static bool __stdcall Settings_Sync(bool, EVRSettingsError* err) {
    if (err) *err = VRSettingsError_None;
    return true;
}

static ChaperoneCalibrationState __stdcall Chap_GetCalibrationState() {
    return ChaperoneCalibrationState_OK;
}

static EVROverlayError __stdcall Overlay_CreateOverlay(char*, char*, VROverlayHandle_t* handle) {
    if (handle) *handle = k_ulOverlayHandleInvalid;
    return VROverlayError_InvalidHandle;
}

static EVRApplicationError __stdcall Apps_GetStartingApplication(char* buf, uint32_t len) {
    if (buf && len) buf[0] = 0;
    return VRApplicationError_None;
}

static EVRRenderModelError __stdcall Models_LoadRenderModelAsync(char*, RenderModel_t**) {
    return VRRenderModelError_Loading;
}
static EVRRenderModelError __stdcall Models_LoadTextureAsync(TextureID_t, RenderModel_TextureMap_t**) {
    return VRRenderModelError_Loading;
}
static EVRRenderModelError __stdcall Models_LoadTextureD3D11Async(TextureID_t, void*, void**) {
    return VRRenderModelError_Loading;
}
static EVRRenderModelError __stdcall Models_LoadIntoTextureD3D11Async(TextureID_t, void*) {
    return VRRenderModelError_Loading;
}

// ---------------------------------------------------------------------------
// Table initialization
// ---------------------------------------------------------------------------
static void InitTables() {
    // System 015
    g_sys015.GetRecommendedRenderTargetSize = Sys_GetRecommendedRenderTargetSize;
    g_sys015.GetProjectionMatrix = Sys_GetProjectionMatrix015;
    g_sys015.GetProjectionRaw = Sys_GetProjectionRaw;
    g_sys015.ComputeDistortion = Sys_ComputeDistortion015;
    g_sys015.GetEyeToHeadTransform = Sys_GetEyeToHeadTransform;
    g_sys015.GetTimeSinceLastVsync = Sys_GetTimeSinceLastVsync;
    g_sys015.GetD3D9AdapterIndex = Sys_GetD3D9AdapterIndex;
    g_sys015.GetDXGIOutputInfo = Sys_GetDXGIOutputInfo;
    g_sys015.IsDisplayOnDesktop = Sys_IsDisplayOnDesktop;
    g_sys015.SetDisplayVisibility = Sys_SetDisplayVisibility;
    g_sys015.GetDeviceToAbsoluteTrackingPose = Sys_GetDeviceToAbsoluteTrackingPose;
    g_sys015.ResetSeatedZeroPose = Sys_ResetSeatedZeroPose;
    g_sys015.GetSeatedZeroPoseToStandingAbsoluteTrackingPose = Sys_GetSeatedZeroPoseToStandingAbsoluteTrackingPose;
    g_sys015.GetRawZeroPoseToStandingAbsoluteTrackingPose = Sys_GetRawZeroPoseToStandingAbsoluteTrackingPose;
    g_sys015.GetSortedTrackedDeviceIndicesOfClass = Sys_GetSortedTrackedDeviceIndicesOfClass;
    g_sys015.GetTrackedDeviceActivityLevel = Sys_GetTrackedDeviceActivityLevel;
    g_sys015.ApplyTransform = Sys_ApplyTransform;
    g_sys015.GetTrackedDeviceIndexForControllerRole = Sys_GetTrackedDeviceIndexForControllerRole;
    g_sys015.GetControllerRoleForTrackedDeviceIndex = Sys_GetControllerRoleForTrackedDeviceIndex;
    g_sys015.GetTrackedDeviceClass = Sys_GetTrackedDeviceClass;
    g_sys015.IsTrackedDeviceConnected = Sys_IsTrackedDeviceConnected;
    g_sys015.GetBoolTrackedDeviceProperty = Sys_GetBoolTrackedDeviceProperty;
    g_sys015.GetFloatTrackedDeviceProperty = Sys_GetFloatTrackedDeviceProperty;
    g_sys015.GetInt32TrackedDeviceProperty = Sys_GetInt32TrackedDeviceProperty;
    g_sys015.GetUint64TrackedDeviceProperty = Sys_GetUint64TrackedDeviceProperty;
    g_sys015.GetMatrix34TrackedDeviceProperty = Sys_GetMatrix34TrackedDeviceProperty;
    g_sys015.GetStringTrackedDeviceProperty = Sys_GetStringTrackedDeviceProperty;
    g_sys015.GetPropErrorNameFromEnum = Sys_GetPropErrorNameFromEnum;
    g_sys015.PollNextEvent = Sys_PollNextEvent;
    g_sys015.PollNextEventWithPose = Sys_PollNextEventWithPose;
    g_sys015.GetEventTypeNameFromEnum = Sys_GetEventTypeNameFromEnum;
    g_sys015.GetHiddenAreaMesh = Sys_GetHiddenAreaMesh015;
    g_sys015.GetControllerState = Sys_GetControllerState015;
    g_sys015.GetControllerStateWithPose = Sys_GetControllerStateWithPose015;
    g_sys015.TriggerHapticPulse = Sys_TriggerHapticPulse;
    g_sys015.GetButtonIdNameFromEnum = Sys_GetButtonIdNameFromEnum;
    g_sys015.GetControllerAxisTypeNameFromEnum = Sys_GetControllerAxisTypeNameFromEnum;
    g_sys015.CaptureInputFocus = Sys_CaptureInputFocus;
    g_sys015.ReleaseInputFocus = Sys_ReleaseInputFocus;
    g_sys015.IsInputFocusCapturedByAnotherProcess = Sys_IsInputFocusCapturedByAnotherProcess;
    g_sys015.DriverDebugRequest = Sys_DriverDebugRequest;
    g_sys015.PerformFirmwareUpdate = Sys_PerformFirmwareUpdate;
    g_sys015.AcknowledgeQuit_Exiting = Sys_AcknowledgeQuit_Exiting;
    g_sys015.AcknowledgeQuit_UserPrompt = Sys_AcknowledgeQuit_UserPrompt;

    // System 012: same layout, some signatures differ
    memcpy(&g_sys012, &g_sys015, sizeof(g_sys012));
    g_sys012.GetProjectionMatrix =
        reinterpret_cast<decltype(g_sys012.GetProjectionMatrix)>(Sys_GetProjectionMatrix012);
    g_sys012.ComputeDistortion =
        reinterpret_cast<decltype(g_sys012.ComputeDistortion)>(Sys_ComputeDistortion012);
    g_sys012.GetHiddenAreaMesh =
        reinterpret_cast<decltype(g_sys012.GetHiddenAreaMesh)>(Sys_GetHiddenAreaMesh012);
    g_sys012.GetControllerState =
        reinterpret_cast<decltype(g_sys012.GetControllerState)>(Sys_GetControllerState012);
    g_sys012.GetControllerStateWithPose =
        reinterpret_cast<decltype(g_sys012.GetControllerStateWithPose)>(Sys_GetControllerStateWithPose012);

    // Compositor 015
    g_comp015.SetTrackingSpace = Comp_SetTrackingSpace;
    g_comp015.GetTrackingSpace = Comp_GetTrackingSpace;
    g_comp015.WaitGetPoses = Comp_WaitGetPoses;
    g_comp015.GetLastPoses = Comp_GetLastPoses;
    g_comp015.GetLastPoseForTrackedDeviceIndex = Comp_GetLastPoseForTrackedDeviceIndex;
    g_comp015.Submit = Comp_Submit;
    g_comp015.ClearLastSubmittedFrame = Comp_ClearLastSubmittedFrame;
    g_comp015.PostPresentHandoff = Comp_PostPresentHandoff;
    g_comp015.GetFrameTiming = Comp_GetFrameTiming;
    g_comp015.GetFrameTimeRemaining = Comp_GetFrameTimeRemaining;
    g_comp015.GetCumulativeStats = Comp_GetCumulativeStats;
    g_comp015.FadeToColor = Comp_FadeToColor;
    g_comp015.FadeGrid = Comp_FadeGrid;
    g_comp015.SetSkyboxOverride = Comp_SetSkyboxOverride;
    g_comp015.ClearSkyboxOverride = Comp_ClearSkyboxOverride;
    g_comp015.CompositorBringToFront = Comp_CompositorBringToFront;
    g_comp015.CompositorGoToBack = Comp_CompositorGoToBack;
    g_comp015.CompositorQuit = Comp_CompositorQuit;
    g_comp015.IsFullscreen = Comp_IsFullscreen;
    g_comp015.GetCurrentSceneFocusProcess = Comp_GetCurrentSceneFocusProcess;
    g_comp015.GetLastFrameRenderer = Comp_GetLastFrameRenderer;
    g_comp015.CanRenderScene = Comp_CanRenderScene;
    g_comp015.ShowMirrorWindow = Comp_ShowMirrorWindow;
    g_comp015.HideMirrorWindow = Comp_HideMirrorWindow;
    g_comp015.IsMirrorWindowVisible = Comp_IsMirrorWindowVisible;
    g_comp015.CompositorDumpImages = Comp_CompositorDumpImages;
    g_comp015.ShouldAppRenderWithLowResources = Comp_ShouldAppRenderWithLowResources;
    g_comp015.ForceInterleavedReprojectionOn = Comp_ForceInterleavedReprojectionOn;
    g_comp015.ForceReconnectProcess = Comp_ForceReconnectProcess;
    g_comp015.SuspendRendering = Comp_SuspendRendering;
    g_comp015.RequestScreenshot = Comp_RequestScreenshot;
    g_comp015.GetCurrentScreenshotType = Comp_GetCurrentScreenshotType;
    g_comp015.GetMirrorTextureD3D11 = Comp_GetMirrorTextureD3D11;
    g_comp015.GetMirrorTextureGL = Comp_GetMirrorTextureGL;
    g_comp015.ReleaseSharedGLTexture = Comp_ReleaseSharedGLTexture;
    g_comp015.LockGLSharedTextureForAccess = Comp_LockGLSharedTextureForAccess;
    g_comp015.UnlockGLSharedTextureForAccess = Comp_UnlockGLSharedTextureForAccess;

    // Compositor 016
    g_comp016.SetTrackingSpace = Comp_SetTrackingSpace;
    g_comp016.GetTrackingSpace = Comp_GetTrackingSpace;
    g_comp016.WaitGetPoses = Comp_WaitGetPoses;
    g_comp016.GetLastPoses = Comp_GetLastPoses;
    g_comp016.GetLastPoseForTrackedDeviceIndex = Comp_GetLastPoseForTrackedDeviceIndex;
    g_comp016.Submit = Comp_Submit;
    g_comp016.ClearLastSubmittedFrame = Comp_ClearLastSubmittedFrame;
    g_comp016.PostPresentHandoff = Comp_PostPresentHandoff;
    g_comp016.GetFrameTiming = Comp_GetFrameTiming;
    g_comp016.GetFrameTimeRemaining = Comp_GetFrameTimeRemaining;
    g_comp016.GetCumulativeStats = Comp_GetCumulativeStats;
    g_comp016.FadeToColor = Comp_FadeToColor;
    g_comp016.FadeGrid = Comp_FadeGrid;
    g_comp016.SetSkyboxOverride = Comp_SetSkyboxOverride;
    g_comp016.ClearSkyboxOverride = Comp_ClearSkyboxOverride;
    g_comp016.CompositorBringToFront = Comp_CompositorBringToFront;
    g_comp016.CompositorGoToBack = Comp_CompositorGoToBack;
    g_comp016.CompositorQuit = Comp_CompositorQuit;
    g_comp016.IsFullscreen = Comp_IsFullscreen;
    g_comp016.GetCurrentSceneFocusProcess = Comp_GetCurrentSceneFocusProcess;
    g_comp016.GetLastFrameRenderer = Comp_GetLastFrameRenderer;
    g_comp016.CanRenderScene = Comp_CanRenderScene;
    g_comp016.ShowMirrorWindow = Comp_ShowMirrorWindow;
    g_comp016.HideMirrorWindow = Comp_HideMirrorWindow;
    g_comp016.IsMirrorWindowVisible = Comp_IsMirrorWindowVisible;
    g_comp016.CompositorDumpImages = Comp_CompositorDumpImages;
    g_comp016.ShouldAppRenderWithLowResources = Comp_ShouldAppRenderWithLowResources;
    g_comp016.ForceInterleavedReprojectionOn = Comp_ForceInterleavedReprojectionOn;
    g_comp016.ForceReconnectProcess = Comp_ForceReconnectProcess;
    g_comp016.SuspendRendering = Comp_SuspendRendering;
    g_comp016.GetMirrorTextureD3D11 = Comp_GetMirrorTextureD3D11;
    g_comp016.GetMirrorTextureGL = Comp_GetMirrorTextureGL;
    g_comp016.ReleaseSharedGLTexture = Comp_ReleaseSharedGLTexture;
    g_comp016.LockGLSharedTextureForAccess = Comp_LockGLSharedTextureForAccess;
    g_comp016.UnlockGLSharedTextureForAccess = Comp_UnlockGLSharedTextureForAccess;

    // Applications (minimal stubs; Rez does not need app management)
    g_apps.AddApplicationManifest = (decltype(g_apps.AddApplicationManifest))StubZero;
    g_apps.RemoveApplicationManifest = (decltype(g_apps.RemoveApplicationManifest))StubZero;
    g_apps.IsApplicationInstalled = (decltype(g_apps.IsApplicationInstalled))StubZero;
    g_apps.GetApplicationCount = (decltype(g_apps.GetApplicationCount))StubZero;
    g_apps.GetApplicationKeyByIndex = (decltype(g_apps.GetApplicationKeyByIndex))StubZero;
    g_apps.GetApplicationKeyByProcessId = (decltype(g_apps.GetApplicationKeyByProcessId))StubZero;
    g_apps.LaunchApplication = (decltype(g_apps.LaunchApplication))StubZero;
    g_apps.LaunchTemplateApplication = (decltype(g_apps.LaunchTemplateApplication))StubZero;
    g_apps.LaunchApplicationFromMimeType = (decltype(g_apps.LaunchApplicationFromMimeType))StubZero;
    g_apps.LaunchDashboardOverlay = (decltype(g_apps.LaunchDashboardOverlay))StubZero;
    g_apps.CancelApplicationLaunch = (decltype(g_apps.CancelApplicationLaunch))StubZero;
    g_apps.IdentifyApplication = (decltype(g_apps.IdentifyApplication))StubZero;
    g_apps.GetApplicationProcessId = (decltype(g_apps.GetApplicationProcessId))StubZero;
    g_apps.GetApplicationsErrorNameFromEnum = (decltype(g_apps.GetApplicationsErrorNameFromEnum))StubEmptyString;
    g_apps.GetApplicationPropertyString = (decltype(g_apps.GetApplicationPropertyString))StubZero;
    g_apps.GetApplicationPropertyBool = (decltype(g_apps.GetApplicationPropertyBool))StubZero;
    g_apps.GetApplicationPropertyUint64 = (decltype(g_apps.GetApplicationPropertyUint64))StubZero;
    g_apps.SetApplicationAutoLaunch = (decltype(g_apps.SetApplicationAutoLaunch))StubZero;
    g_apps.GetApplicationAutoLaunch = (decltype(g_apps.GetApplicationAutoLaunch))StubZero;
    g_apps.SetDefaultApplicationForMimeType = (decltype(g_apps.SetDefaultApplicationForMimeType))StubZero;
    g_apps.GetDefaultApplicationForMimeType = (decltype(g_apps.GetDefaultApplicationForMimeType))StubZero;
    g_apps.GetApplicationSupportedMimeTypes = (decltype(g_apps.GetApplicationSupportedMimeTypes))StubZero;
    g_apps.GetApplicationsThatSupportMimeType = (decltype(g_apps.GetApplicationsThatSupportMimeType))StubZero;
    g_apps.GetApplicationLaunchArguments = (decltype(g_apps.GetApplicationLaunchArguments))StubZero;
    g_apps.GetStartingApplication = Apps_GetStartingApplication;
    g_apps.GetTransitionState = (decltype(g_apps.GetTransitionState))StubZero;
    g_apps.PerformApplicationPrelaunchCheck = (decltype(g_apps.PerformApplicationPrelaunchCheck))StubZero;
    g_apps.GetApplicationsTransitionStateNameFromEnum =
        (decltype(g_apps.GetApplicationsTransitionStateNameFromEnum))StubEmptyString;
    g_apps.IsQuitUserPromptRequested = (decltype(g_apps.IsQuitUserPromptRequested))StubZero;
    g_apps.LaunchInternalProcess = (decltype(g_apps.LaunchInternalProcess))StubZero;

    // Render models
    g_models.LoadRenderModel_Async = Models_LoadRenderModelAsync;
    g_models.FreeRenderModel = (decltype(g_models.FreeRenderModel))StubVoid;
    g_models.LoadTexture_Async = Models_LoadTextureAsync;
    g_models.FreeTexture = (decltype(g_models.FreeTexture))StubVoid;
    g_models.LoadTextureD3D11_Async = Models_LoadTextureD3D11Async;
    g_models.LoadIntoTextureD3D11_Async = Models_LoadIntoTextureD3D11Async;
    g_models.FreeTextureD3D11 = (decltype(g_models.FreeTextureD3D11))StubVoid;
    g_models.GetRenderModelName = (decltype(g_models.GetRenderModelName))StubZero;
    g_models.GetRenderModelCount = (decltype(g_models.GetRenderModelCount))StubZero;
    g_models.GetComponentCount = (decltype(g_models.GetComponentCount))StubZero;
    g_models.GetComponentName = (decltype(g_models.GetComponentName))StubZero;
    g_models.GetComponentButtonMask = (decltype(g_models.GetComponentButtonMask))StubZero;
    g_models.GetComponentRenderModelName = (decltype(g_models.GetComponentRenderModelName))StubZero;
    g_models.GetComponentState = (decltype(g_models.GetComponentState))StubZero;
    g_models.RenderModelHasComponent = (decltype(g_models.RenderModelHasComponent))StubZero;
    g_models.GetRenderModelThumbnailURL = (decltype(g_models.GetRenderModelThumbnailURL))StubZero;
    g_models.GetRenderModelOriginalPath = (decltype(g_models.GetRenderModelOriginalPath))StubZero;
    g_models.GetRenderModelErrorNameFromEnum = (decltype(g_models.GetRenderModelErrorNameFromEnum))StubEmptyString;

    // Settings
    g_settings.GetSettingsErrorNameFromEnum = (decltype(g_settings.GetSettingsErrorNameFromEnum))StubEmptyString;
    g_settings.Sync = Settings_Sync;
    g_settings.GetBool = Settings_GetBool;
    g_settings.SetBool = (decltype(g_settings.SetBool))StubVoid;
    g_settings.GetInt32 = Settings_GetInt32;
    g_settings.SetInt32 = (decltype(g_settings.SetInt32))StubVoid;
    g_settings.GetFloat = Settings_GetFloat;
    g_settings.SetFloat = (decltype(g_settings.SetFloat))StubVoid;
    g_settings.GetString = Settings_GetString;
    g_settings.SetString = (decltype(g_settings.SetString))StubVoid;
    g_settings.RemoveSection = (decltype(g_settings.RemoveSection))StubVoid;
    g_settings.RemoveKeyInSection = (decltype(g_settings.RemoveKeyInSection))StubVoid;

    // Chaperone
    g_chap.GetCalibrationState = Chap_GetCalibrationState;
    g_chap.GetPlayAreaSize = (decltype(g_chap.GetPlayAreaSize))StubZero;
    g_chap.GetPlayAreaRect = (decltype(g_chap.GetPlayAreaRect))StubZero;
    g_chap.ReloadInfo = (decltype(g_chap.ReloadInfo))StubVoid;
    g_chap.SetSceneColor = (decltype(g_chap.SetSceneColor))StubVoid;
    g_chap.GetBoundsColor = (decltype(g_chap.GetBoundsColor))StubVoid;
    g_chap.AreBoundsVisible = (decltype(g_chap.AreBoundsVisible))StubZero;
    g_chap.ForceBoundsVisible = (decltype(g_chap.ForceBoundsVisible))StubVoid;

    // Chaperone setup
    g_chapSetup.CommitWorkingCopy = (decltype(g_chapSetup.CommitWorkingCopy))StubZero;
    g_chapSetup.RevertWorkingCopy = (decltype(g_chapSetup.RevertWorkingCopy))StubVoid;
    g_chapSetup.GetWorkingPlayAreaSize = (decltype(g_chapSetup.GetWorkingPlayAreaSize))StubZero;
    g_chapSetup.GetWorkingPlayAreaRect = (decltype(g_chapSetup.GetWorkingPlayAreaRect))StubZero;
    g_chapSetup.GetWorkingCollisionBoundsInfo =
        (decltype(g_chapSetup.GetWorkingCollisionBoundsInfo))StubZero;
    g_chapSetup.GetLiveCollisionBoundsInfo = (decltype(g_chapSetup.GetLiveCollisionBoundsInfo))StubZero;
    g_chapSetup.GetWorkingSeatedZeroPoseToRawTrackingPose =
        (decltype(g_chapSetup.GetWorkingSeatedZeroPoseToRawTrackingPose))StubZero;
    g_chapSetup.GetWorkingStandingZeroPoseToRawTrackingPose =
        (decltype(g_chapSetup.GetWorkingStandingZeroPoseToRawTrackingPose))StubZero;
    g_chapSetup.SetWorkingPlayAreaSize = (decltype(g_chapSetup.SetWorkingPlayAreaSize))StubVoid;
    g_chapSetup.SetWorkingCollisionBoundsInfo = (decltype(g_chapSetup.SetWorkingCollisionBoundsInfo))StubVoid;
    g_chapSetup.SetWorkingSeatedZeroPoseToRawTrackingPose =
        (decltype(g_chapSetup.SetWorkingSeatedZeroPoseToRawTrackingPose))StubVoid;
    g_chapSetup.SetWorkingStandingZeroPoseToRawTrackingPose =
        (decltype(g_chapSetup.SetWorkingStandingZeroPoseToRawTrackingPose))StubVoid;
    g_chapSetup.ReloadFromDisk = (decltype(g_chapSetup.ReloadFromDisk))StubVoid;
    g_chapSetup.GetLiveSeatedZeroPoseToRawTrackingPose =
        (decltype(g_chapSetup.GetLiveSeatedZeroPoseToRawTrackingPose))StubZero;
    g_chapSetup.SetWorkingCollisionBoundsTagsInfo =
        (decltype(g_chapSetup.SetWorkingCollisionBoundsTagsInfo))StubVoid;
    g_chapSetup.GetLiveCollisionBoundsTagsInfo =
        (decltype(g_chapSetup.GetLiveCollisionBoundsTagsInfo))StubZero;
    g_chapSetup.SetWorkingPhysicalBoundsInfo = (decltype(g_chapSetup.SetWorkingPhysicalBoundsInfo))StubZero;
    g_chapSetup.GetLivePhysicalBoundsInfo = (decltype(g_chapSetup.GetLivePhysicalBoundsInfo))StubZero;
    g_chapSetup.ExportLiveToBuffer = (decltype(g_chapSetup.ExportLiveToBuffer))StubZero;
    g_chapSetup.ImportFromBufferToWorking = (decltype(g_chapSetup.ImportFromBufferToWorking))StubZero;

    // Overlay (stubs)
    g_overlay.FindOverlay = (decltype(g_overlay.FindOverlay))StubZero;
    g_overlay.CreateOverlay = Overlay_CreateOverlay;
    g_overlay.DestroyOverlay = (decltype(g_overlay.DestroyOverlay))StubZero;
    g_overlay.SetHighQualityOverlay = (decltype(g_overlay.SetHighQualityOverlay))StubZero;
    g_overlay.GetHighQualityOverlay = (decltype(g_overlay.GetHighQualityOverlay))StubZero;
    g_overlay.GetOverlayKey = (decltype(g_overlay.GetOverlayKey))StubZero;
    g_overlay.GetOverlayName = (decltype(g_overlay.GetOverlayName))StubZero;
    g_overlay.GetOverlayImageData = (decltype(g_overlay.GetOverlayImageData))StubZero;
    g_overlay.GetOverlayErrorNameFromEnum = (decltype(g_overlay.GetOverlayErrorNameFromEnum))StubEmptyString;
    g_overlay.SetOverlayRenderingPid = (decltype(g_overlay.SetOverlayRenderingPid))StubZero;
    g_overlay.GetOverlayRenderingPid = (decltype(g_overlay.GetOverlayRenderingPid))StubZero;
    g_overlay.SetOverlayFlag = (decltype(g_overlay.SetOverlayFlag))StubZero;
    g_overlay.GetOverlayFlag = (decltype(g_overlay.GetOverlayFlag))StubZero;
    g_overlay.SetOverlayColor = (decltype(g_overlay.SetOverlayColor))StubZero;
    g_overlay.GetOverlayColor = (decltype(g_overlay.GetOverlayColor))StubZero;
    g_overlay.SetOverlayAlpha = (decltype(g_overlay.SetOverlayAlpha))StubZero;
    g_overlay.GetOverlayAlpha = (decltype(g_overlay.GetOverlayAlpha))StubZero;
    g_overlay.SetOverlayTexelAspect = (decltype(g_overlay.SetOverlayTexelAspect))StubZero;
    g_overlay.GetOverlayTexelAspect = (decltype(g_overlay.GetOverlayTexelAspect))StubZero;
    g_overlay.SetOverlaySortOrder = (decltype(g_overlay.SetOverlaySortOrder))StubZero;
    g_overlay.GetOverlaySortOrder = (decltype(g_overlay.GetOverlaySortOrder))StubZero;
    g_overlay.SetOverlayWidthInMeters = (decltype(g_overlay.SetOverlayWidthInMeters))StubZero;
    g_overlay.GetOverlayWidthInMeters = (decltype(g_overlay.GetOverlayWidthInMeters))StubZero;
    g_overlay.SetOverlayAutoCurveDistanceRangeInMeters =
        (decltype(g_overlay.SetOverlayAutoCurveDistanceRangeInMeters))StubZero;
    g_overlay.GetOverlayAutoCurveDistanceRangeInMeters =
        (decltype(g_overlay.GetOverlayAutoCurveDistanceRangeInMeters))StubZero;
    g_overlay.SetOverlayTextureColorSpace = (decltype(g_overlay.SetOverlayTextureColorSpace))StubZero;
    g_overlay.GetOverlayTextureColorSpace = (decltype(g_overlay.GetOverlayTextureColorSpace))StubZero;
    g_overlay.SetOverlayTextureBounds = (decltype(g_overlay.SetOverlayTextureBounds))StubZero;
    g_overlay.GetOverlayTextureBounds = (decltype(g_overlay.GetOverlayTextureBounds))StubZero;
    g_overlay.GetOverlayTransformType = (decltype(g_overlay.GetOverlayTransformType))StubZero;
    g_overlay.SetOverlayTransformAbsolute = (decltype(g_overlay.SetOverlayTransformAbsolute))StubZero;
    g_overlay.GetOverlayTransformAbsolute = (decltype(g_overlay.GetOverlayTransformAbsolute))StubZero;
    g_overlay.SetOverlayTransformTrackedDeviceRelative =
        (decltype(g_overlay.SetOverlayTransformTrackedDeviceRelative))StubZero;
    g_overlay.GetOverlayTransformTrackedDeviceRelative =
        (decltype(g_overlay.GetOverlayTransformTrackedDeviceRelative))StubZero;
    g_overlay.SetOverlayTransformTrackedDeviceComponent =
        (decltype(g_overlay.SetOverlayTransformTrackedDeviceComponent))StubZero;
    g_overlay.GetOverlayTransformTrackedDeviceComponent =
        (decltype(g_overlay.GetOverlayTransformTrackedDeviceComponent))StubZero;
    g_overlay.ShowOverlay = (decltype(g_overlay.ShowOverlay))StubZero;
    g_overlay.HideOverlay = (decltype(g_overlay.HideOverlay))StubZero;
    g_overlay.IsOverlayVisible = (decltype(g_overlay.IsOverlayVisible))StubZero;
    g_overlay.GetTransformForOverlayCoordinates = (decltype(g_overlay.GetTransformForOverlayCoordinates))StubZero;
    g_overlay.PollNextOverlayEvent = (decltype(g_overlay.PollNextOverlayEvent))StubZero;
    g_overlay.GetOverlayInputMethod = (decltype(g_overlay.GetOverlayInputMethod))StubZero;
    g_overlay.SetOverlayInputMethod = (decltype(g_overlay.SetOverlayInputMethod))StubZero;
    g_overlay.GetOverlayMouseScale = (decltype(g_overlay.GetOverlayMouseScale))StubZero;
    g_overlay.SetOverlayMouseScale = (decltype(g_overlay.SetOverlayMouseScale))StubZero;
    g_overlay.ComputeOverlayIntersection = (decltype(g_overlay.ComputeOverlayIntersection))StubZero;
    g_overlay.HandleControllerOverlayInteractionAsMouse =
        (decltype(g_overlay.HandleControllerOverlayInteractionAsMouse))StubZero;
    g_overlay.IsHoverTargetOverlay = (decltype(g_overlay.IsHoverTargetOverlay))StubZero;
    g_overlay.GetGamepadFocusOverlay = (decltype(g_overlay.GetGamepadFocusOverlay))StubZero;
    g_overlay.SetGamepadFocusOverlay = (decltype(g_overlay.SetGamepadFocusOverlay))StubZero;
    g_overlay.SetOverlayNeighbor = (decltype(g_overlay.SetOverlayNeighbor))StubZero;
    g_overlay.MoveGamepadFocusToNeighbor = (decltype(g_overlay.MoveGamepadFocusToNeighbor))StubZero;
    g_overlay.SetOverlayTexture = (decltype(g_overlay.SetOverlayTexture))StubZero;
    g_overlay.ClearOverlayTexture = (decltype(g_overlay.ClearOverlayTexture))StubZero;
    g_overlay.SetOverlayRaw = (decltype(g_overlay.SetOverlayRaw))StubZero;
    g_overlay.SetOverlayFromFile = (decltype(g_overlay.SetOverlayFromFile))StubZero;
    g_overlay.GetOverlayTexture = (decltype(g_overlay.GetOverlayTexture))StubZero;
    g_overlay.ReleaseNativeOverlayHandle = (decltype(g_overlay.ReleaseNativeOverlayHandle))StubZero;
    g_overlay.GetOverlayTextureSize = (decltype(g_overlay.GetOverlayTextureSize))StubZero;
    g_overlay.CreateDashboardOverlay = (decltype(g_overlay.CreateDashboardOverlay))StubZero;
    g_overlay.IsDashboardVisible = (decltype(g_overlay.IsDashboardVisible))StubZero;
    g_overlay.IsActiveDashboardOverlay = (decltype(g_overlay.IsActiveDashboardOverlay))StubZero;
    g_overlay.SetDashboardOverlaySceneProcess = (decltype(g_overlay.SetDashboardOverlaySceneProcess))StubZero;
    g_overlay.GetDashboardOverlaySceneProcess = (decltype(g_overlay.GetDashboardOverlaySceneProcess))StubZero;
    g_overlay.ShowDashboard = (decltype(g_overlay.ShowDashboard))StubVoid;
    g_overlay.GetPrimaryDashboardDevice = (decltype(g_overlay.GetPrimaryDashboardDevice))StubZero;
    g_overlay.ShowKeyboard = (decltype(g_overlay.ShowKeyboard))StubZero;
    g_overlay.ShowKeyboardForOverlay = (decltype(g_overlay.ShowKeyboardForOverlay))StubZero;
    g_overlay.GetKeyboardText = (decltype(g_overlay.GetKeyboardText))StubZero;
    g_overlay.HideKeyboard = (decltype(g_overlay.HideKeyboard))StubVoid;
    g_overlay.SetKeyboardTransformAbsolute = (decltype(g_overlay.SetKeyboardTransformAbsolute))StubVoid;
    g_overlay.SetKeyboardPositionForOverlay = (decltype(g_overlay.SetKeyboardPositionForOverlay))StubVoid;
}

// Auto-generated C++ interface objects (correct vtable/this ABI)
#include "interface_impls.inc"

// ---------------------------------------------------------------------------
// Exported flat API
// ---------------------------------------------------------------------------
static bool IsSupportedInterface(const char* name) {
    if (!name) return false;
    if (strncmp(name, "FnTable:", 8) == 0) name += 8;
    static const char* supported[] = {
        "IVRSystem_012", "IVRSystem_013", "IVRSystem_014", "IVRSystem_015",
        "IVRCompositor_015", "IVRCompositor_016",
        "IVRApplications_006",
        "IVRRenderModels_005",
        "IVRSettings_001",
        "IVRChaperone_003",
        "IVRChaperoneSetup_005",
        "IVROverlay_013",
    };
    for (const char* s : supported) {
        if (strcmp(name, s) == 0) return true;
    }
    return false;
}

static void* GetInterface(const char* name) {
    if (!name) return nullptr;
    bool fnTable = false;
    if (strncmp(name, "FnTable:", 8) == 0) {
        fnTable = true;
        name += 8;
    }
    void* table = nullptr;
    void* object = nullptr;
    if (strcmp(name, "IVRSystem_012") == 0 || strcmp(name, "IVRSystem_013") == 0 ||
        strcmp(name, "IVRSystem_014") == 0) {
        table = &g_sys012;
        object = &g_impl_g_sys012;
    } else if (strcmp(name, "IVRSystem_015") == 0) {
        table = &g_sys015;
        object = &g_impl_g_sys015;
    } else if (strcmp(name, "IVRCompositor_015") == 0) {
        table = &g_comp015;
        object = &g_impl_g_comp015;
    } else if (strcmp(name, "IVRCompositor_016") == 0) {
        table = &g_comp016;
        object = &g_impl_g_comp016;
    } else if (strcmp(name, "IVRApplications_006") == 0) {
        table = &g_apps;
        object = &g_impl_g_apps;
    } else if (strcmp(name, "IVRRenderModels_005") == 0) {
        table = &g_models;
        object = &g_impl_g_models;
    } else if (strcmp(name, "IVRSettings_001") == 0) {
        table = &g_settings;
        object = &g_impl_g_settings;
    } else if (strcmp(name, "IVRChaperone_003") == 0) {
        table = &g_chap;
        object = &g_impl_g_chap;
    } else if (strcmp(name, "IVRChaperoneSetup_005") == 0) {
        table = &g_chapSetup;
        object = &g_impl_g_chapSetup;
    } else if (strcmp(name, "IVROverlay_013") == 0) {
        table = &g_overlay;
        object = &g_impl_g_overlay;
    }
    if (!table) return nullptr;
    return fnTable ? table : object;
}

extern "C" __declspec(dllexport) intptr_t VR_InitInternal(EVRInitError* peError, EVRApplicationType eType,
                                                          const char* pStartupInfo) {
    InitRuntimeConfig();
    std::call_once(g_initTablesOnce, InitTables);
    if (peError) *peError = VRInitError_None;
    LogPrintf("VR_InitInternal: type=%d", (int)eType);
    (void)pStartupInfo;
    g_token++;
    if (!g_noPresenter) Presenter::Get().Start();
    return g_token;
}

extern "C" __declspec(dllexport) void VR_ShutdownInternal() {
    LogPrintf("VR_ShutdownInternal");
    Presenter::Get().Stop();
}

extern "C" __declspec(dllexport) bool VR_IsHmdPresent() { return true; }
extern "C" __declspec(dllexport) bool VR_IsRuntimeInstalled() { return true; }
extern "C" __declspec(dllexport) const char* VR_RuntimePath() { return g_dllDir; }

extern "C" __declspec(dllexport) intptr_t VR_GetGenericInterface(const char* pchInterfaceVersion,
                                                                 EVRInitError* peError) {
    InitRuntimeConfig();
    std::call_once(g_initTablesOnce, InitTables);
    void* p = GetInterface(pchInterfaceVersion);
    if (p) {
        if (peError) *peError = VRInitError_None;
        LogPrintf("GetGenericInterface: %s -> ok", pchInterfaceVersion);
    } else {
        if (peError) *peError = VRInitError_Init_InterfaceNotFound;
        LogPrintf("GetGenericInterface: %s -> NOT FOUND", pchInterfaceVersion ? pchInterfaceVersion : "(null)");
    }
    return reinterpret_cast<intptr_t>(p);
}

extern "C" __declspec(dllexport) bool VR_IsInterfaceVersionValid(const char* pchInterfaceVersion) {
    InitRuntimeConfig();
    bool ok = IsSupportedInterface(pchInterfaceVersion);
    LogPrintf("IsInterfaceVersionValid: %s -> %d", pchInterfaceVersion ? pchInterfaceVersion : "(null)",
              ok ? 1 : 0);
    return ok;
}

extern "C" __declspec(dllexport) uint32_t VR_GetInitToken() { return (uint32_t)g_token; }

extern "C" __declspec(dllexport) const char* VR_GetVRInitErrorAsSymbol(EVRInitError error) {
    return error == VRInitError_None ? "VRInitError_None" : "VRInitError_Unknown";
}

extern "C" __declspec(dllexport) const char* VR_GetVRInitErrorAsEnglishDescription(EVRInitError error) {
    return error == VRInitError_None ? "None" : "Unknown error";
}

extern "C" __declspec(dllexport) const char* VR_GetStringForHmdError(EVRInitError error) {
    return VR_GetVRInitErrorAsSymbol(error);
}

// ---------------------------------------------------------------------------
// DLL entry
// ---------------------------------------------------------------------------
BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_hModule = module;
        DisableThreadLibraryCalls(module);
        char path[MAX_PATH];
        GetModuleFileNameA(module, path, MAX_PATH);
        char* slash = strrchr(path, '\\');
        if (slash) *slash = 0;
        char logPath[MAX_PATH];
        sprintf_s(logPath, "%s\\rez3d.log", path);
        LogInit(logPath);
        LogPrintf("openvr_api shim loaded (%s)", path);
    } else if (reason == DLL_PROCESS_DETACH) {
        Presenter::Get().StopForProcessExit();
        LogPrintf("openvr_api shim unloading");
        LogClose();
    }
    return TRUE;
}
