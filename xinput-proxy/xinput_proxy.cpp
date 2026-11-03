// Local XInput1_3 proxy for Rez Infinite stereo port.
// - Forwards to the system XInput1_3.dll
// - Presents a virtual gamepad driven by the keyboard and/or rezpad.txt
//   (needed because Rez's VR mode has no window and is gamepad-only)
// - Bridges DirectInput/HID gamepads (Logitech, DualShock, generic USB pads)
//   that Rez cannot see, as XInput pad 1, through the Windows joystick API

#include <windows.h>
#include <tlhelp32.h>
#include <mmsystem.h>
#include <xinput.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "../src/reticle_state.h"

#pragma comment(lib, "winmm.lib")

#ifndef ERROR_EMPTY
#define ERROR_EMPTY 4306L
#endif

static HMODULE g_self = nullptr;
static HMODULE g_real = nullptr;
static CRITICAL_SECTION g_cs;
static bool g_csInit = false;
static char g_dir[MAX_PATH] = {};
static bool g_loggedLoad = false;

typedef DWORD(WINAPI* PFN_GetState)(DWORD, XINPUT_STATE*);
typedef DWORD(WINAPI* PFN_SetState)(DWORD, XINPUT_VIBRATION*);
typedef DWORD(WINAPI* PFN_GetCaps)(DWORD, DWORD, XINPUT_CAPABILITIES*);
typedef DWORD(WINAPI* PFN_GetBattery)(DWORD, BYTE, XINPUT_BATTERY_INFORMATION*);
typedef DWORD(WINAPI* PFN_GetKeystroke)(DWORD, DWORD, PXINPUT_KEYSTROKE);
typedef void(WINAPI* PFN_Enable)(BOOL);

static void Log(const char* fmt, ...) {
    if (!g_csInit) return;
    char path[MAX_PATH];
    sprintf_s(path, "%s\\xinput_proxy.log", g_dir);
    EnterCriticalSection(&g_cs);
    FILE* f = nullptr;
    fopen_s(&f, path, "a");
    if (f) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        fprintf(f, "[%02u:%02u:%02u.%03u] ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
        va_list ap;
        va_start(ap, fmt);
        vfprintf(f, fmt, ap);
        va_end(ap);
        fputc('\n', f);
        fclose(f);
    }
    LeaveCriticalSection(&g_cs);
}

static void EnsureReal() {
    if (g_real) return;
    char sys[MAX_PATH];
    GetSystemDirectoryA(sys, MAX_PATH);
    strcat_s(sys, "\\xinput1_3.dll");
    g_real = LoadLibraryA(sys);
}

static void EnsureInit() {
    if (g_csInit) return;
    InitializeCriticalSection(&g_cs);
    g_csInit = true;
    if (g_self) {
        GetModuleFileNameA(g_self, g_dir, MAX_PATH);
        char* slash = strrchr(g_dir, '\\');
        if (slash) *slash = 0;
    }
}

static bool GetPadFilePath(char* out, size_t count) {
    if (!g_dir[0]) return false;
    sprintf_s(out, count, "%s\\rezpad.txt", g_dir);
    return true;
}

// Keyboard-as-pad only while this process has focus (typing in other apps must
// not drive the game). REZPAD_NO_KEYBOARD=1 turns it off (the launcher sets it,
// since it handles its own keys).
// True when the foreground window belongs to the game: this process or one of the
// processes that started it. Rez-infinite.exe owns the fullscreen window the game
// draws in (and Area X adds a bootstrap process in between), so the foreground
// window is usually not this process's own.
static bool GameHasFocus() {
    static DWORD family[4] = {};
    static int count = -1;
    if (count < 0) {
        count = 0;
        family[count++] = GetCurrentProcessId();
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap != INVALID_HANDLE_VALUE) {
            for (int level = 0; level < 3 && count < 4; level++) {
                DWORD child = family[count - 1], parent = 0;
                PROCESSENTRY32W pe{ sizeof(pe) };
                for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe))
                    if (pe.th32ProcessID == child) { parent = pe.th32ParentProcessID; break; }
                if (!parent) break;
                // Stop at the shell or a launcher of ours: only game processes count
                bool game = false;
                for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe))
                    if (pe.th32ProcessID == parent) {
                        game = _wcsnicmp(pe.szExeFile, L"Rez", 3) == 0;
                        break;
                    }
                if (!game) break;
                family[count++] = parent;
            }
            CloseHandle(snap);
        }
    }
    HWND fg = GetForegroundWindow();
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    for (int i = 0; i < count; i++)
        if (pid == family[i]) return true;
    // Area X: REZ.exe started it and exited, so the launcher whose window hosts the
    // game's isn't a parent. A foreground window that hosts one of ours counts.
    static HWND lastFg = nullptr;
    static ULONGLONG lastCheck = 0;
    static bool hosts = false;
    const ULONGLONG now = GetTickCount64();
    if (fg != lastFg || now - lastCheck > 1000) {
        lastFg = fg;
        lastCheck = now;
        struct Ctx { DWORD self; bool found; } ctx{ GetCurrentProcessId(), false };
        if (fg)
            EnumChildWindows(fg, [](HWND h, LPARAM lp) -> BOOL {
                auto* c = reinterpret_cast<Ctx*>(lp);
                DWORD p = 0;
                GetWindowThreadProcessId(h, &p);
                if (p == c->self) { c->found = true; return FALSE; }
                return TRUE;
            }, (LPARAM)&ctx);
        hosts = ctx.found;
    }
    return hosts;
}

static bool KeyboardAllowed() {
    static int disabled = -1;
    if (disabled < 0) {
        char v[8] = {};
        disabled = GetEnvironmentVariableA("REZPAD_NO_KEYBOARD", v, sizeof(v)) > 0 && v[0] != '0' ? 1 : 0;
    }
    if (disabled) return false;
    return GameHasFocus();
}

// Right-stick directions from the file trigger (RLEFT / RRIGHT / RUP / RDOWN)
static short g_fileRX = 0, g_fileRY = 0, g_fileLX = 0, g_fileLY = 0;

static unsigned short VirtualButtons() {
    unsigned short b = 0;
    g_fileRX = g_fileRY = g_fileLX = g_fileLY = 0;
    const bool kb = KeyboardAllowed();

    // Keyboard mapping
    if (kb) {
    if (GetAsyncKeyState(VK_RETURN) & 0x8000) b |= XINPUT_GAMEPAD_A;
    if (GetAsyncKeyState(VK_SPACE) & 0x8000) b |= XINPUT_GAMEPAD_A;
    if (GetAsyncKeyState('Z') & 0x8000) b |= XINPUT_GAMEPAD_A;
    if (GetAsyncKeyState('X') & 0x8000) b |= XINPUT_GAMEPAD_B;
    if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) b |= XINPUT_GAMEPAD_B;
    if (GetAsyncKeyState('C') & 0x8000) b |= XINPUT_GAMEPAD_X;
    if (GetAsyncKeyState('V') & 0x8000) b |= XINPUT_GAMEPAD_Y;
    if (GetAsyncKeyState('Q') & 0x8000) b |= XINPUT_GAMEPAD_LEFT_SHOULDER;
    if (GetAsyncKeyState('E') & 0x8000) b |= XINPUT_GAMEPAD_RIGHT_SHOULDER;
    if (GetAsyncKeyState(VK_BACK) & 0x8000) b |= XINPUT_GAMEPAD_BACK;
    if (GetAsyncKeyState(VK_UP) & 0x8000) b |= XINPUT_GAMEPAD_DPAD_UP;
    if (GetAsyncKeyState(VK_DOWN) & 0x8000) b |= XINPUT_GAMEPAD_DPAD_DOWN;
    if (GetAsyncKeyState(VK_LEFT) & 0x8000) b |= XINPUT_GAMEPAD_DPAD_LEFT;
    if (GetAsyncKeyState(VK_RIGHT) & 0x8000) b |= XINPUT_GAMEPAD_DPAD_RIGHT;
    }

    // File trigger (used by automation / tests)
    char path[MAX_PATH];
    if (GetPadFilePath(path, MAX_PATH)) {
        FILE* f = nullptr;
        fopen_s(&f, path, "r");
        if (f) {
            char line[256] = {};
            fgets(line, sizeof(line), f);
            fclose(f);
            for (char* p = line; *p; p++) *p = (char)toupper((unsigned char)*p);
            char token[32];
            size_t ti = 0;
            for (size_t i = 0;; i++) {
                char c = line[i];
                bool sep = (c == 0 || c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == ',');
                if (sep && ti > 0) {
                    token[ti] = 0;
                    if (strcmp(token, "A") == 0) b |= XINPUT_GAMEPAD_A;
                    else if (strcmp(token, "B") == 0) b |= XINPUT_GAMEPAD_B;
                    else if (strcmp(token, "X") == 0) b |= XINPUT_GAMEPAD_X;
                    else if (strcmp(token, "Y") == 0) b |= XINPUT_GAMEPAD_Y;
                    else if (strcmp(token, "START") == 0) b |= XINPUT_GAMEPAD_START;
                    else if (strcmp(token, "BACK") == 0) b |= XINPUT_GAMEPAD_BACK;
                    else if (strcmp(token, "UP") == 0) b |= XINPUT_GAMEPAD_DPAD_UP;
                    else if (strcmp(token, "DOWN") == 0) b |= XINPUT_GAMEPAD_DPAD_DOWN;
                    else if (strcmp(token, "LEFT") == 0) b |= XINPUT_GAMEPAD_DPAD_LEFT;
                    else if (strcmp(token, "RIGHT") == 0) b |= XINPUT_GAMEPAD_DPAD_RIGHT;
                    else if (strcmp(token, "LB") == 0) b |= XINPUT_GAMEPAD_LEFT_SHOULDER;
                    else if (strcmp(token, "RB") == 0) b |= XINPUT_GAMEPAD_RIGHT_SHOULDER;
                    else if (strcmp(token, "RS") == 0) b |= XINPUT_GAMEPAD_RIGHT_THUMB;
                    else if (strcmp(token, "LS") == 0) b |= XINPUT_GAMEPAD_LEFT_THUMB;
                    else if (strcmp(token, "RLEFT") == 0) g_fileRX = -16000;
                    else if (strcmp(token, "RRIGHT") == 0) g_fileRX = 16000;
                    else if (strcmp(token, "RUP") == 0) g_fileRY = 16000;
                    else if (strcmp(token, "RDOWN") == 0) g_fileRY = -16000;
                    // Exact stick values for tests: LX=-1 .. LX=1 (also LY, RX, RY)
                    else if (strchr(token, '=')) {
                        const char* eq = strchr(token, '=');
                        float v = (float)atof(eq + 1);
                        v = v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v);
                        const short sv = (short)(v * 32767.0f);
                        if (strncmp(token, "LX=", 3) == 0) g_fileLX = sv;
                        else if (strncmp(token, "LY=", 3) == 0) g_fileLY = sv;
                        else if (strncmp(token, "RX=", 3) == 0) g_fileRX = sv;
                        else if (strncmp(token, "RY=", 3) == 0) g_fileRY = sv;
                    }
                    ti = 0;
                } else if (!sep && ti < sizeof(token) - 1) {
                    token[ti++] = c;
                }
                if (c == 0) break;
            }
        }
    }
    return b;
}

static void VirtualSticks(short& lx, short& ly, short& rx, short& ry) {
    lx = ly = rx = ry = 0;
    if (KeyboardAllowed()) {
    if (GetAsyncKeyState('A') & 0x8000) lx = -16000;
    if (GetAsyncKeyState('D') & 0x8000) lx = 16000;
    if (GetAsyncKeyState('W') & 0x8000) ly = 16000;
    if (GetAsyncKeyState('S') & 0x8000) ly = -16000;
    if (GetAsyncKeyState('J') & 0x8000) rx = -16000;
    if (GetAsyncKeyState('L') & 0x8000) rx = 16000;
    if (GetAsyncKeyState('I') & 0x8000) ry = 16000;
    if (GetAsyncKeyState('K') & 0x8000) ry = -16000;
    }

    // Menu/file navigation also drives the left stick so either style works.
    unsigned short b = VirtualButtons();
    if (b & XINPUT_GAMEPAD_DPAD_LEFT) lx = -16000;
    if (b & XINPUT_GAMEPAD_DPAD_RIGHT) lx = 16000;
    if (b & XINPUT_GAMEPAD_DPAD_UP) ly = 16000;
    if (b & XINPUT_GAMEPAD_DPAD_DOWN) ly = -16000;
    if (g_fileLX) lx = g_fileLX;
    if (g_fileLY) ly = g_fileLY;
    if (g_fileRX) rx = g_fileRX;
    if (g_fileRY) ry = g_fileRY;
}

// ---------------------------------------------------------------------------
// DirectInput/HID gamepad bridge
// ---------------------------------------------------------------------------
// 1-based joystick button numbers for each XInput button (0 = unmapped), and
// the joystick axes (X Y Z R U V) used for the right stick.
struct PadMap {
    int a = 2, b = 3, x = 1, y = 4, lb = 5, rb = 6, lt = 7, rt = 8, back = 9, start = 10, ls = 11, rs = 12;
    char rightX = 'Z', rightY = 'R';
    float deadzone = 0.15f;
    float stickSpeed = 0.6f;   // PadStickSpeed: % of full deflection the game sees (below its slowest setting)
    float stickCurve = 1.5f;   // PadStickCurve: >1 = finer control near the centre
    float gameDeadzone = 0.24f;  // PadGameDeadzone: the game's own aim dead zone (XInput's standard 24 %)
    bool fireOnRB = false;     // PadFireOnRB: the right bumper fires (A), freeing the thumb for the right stick
    bool recenter = true;      // PadRecenter: L3 brings the reticle back to the middle (Rez)
    int rezSpeed = -1;         // PadRezSpeed: Rez's own gamepad speed for the recenter (auto = read it)
    bool keepInView = true;    // ReticleKeepInView: stop the aim stick at the edges of the picture (Rez)
    bool reticleLog = false;   // PadReticleLog: log every reticle measurement with the stick sent (testing)
    bool enabled = true;
};

// Logitech Dual Action / F310 (D mode), DualShock 4 and most generic pads share
// the default order: 1 = left face, 2 = bottom, 3 = right, 4 = top.
// Switch Pro over DirectInput: 1 = B (bottom), 2 = A (right), 3 = Y (left), 4 = X (top).
static PadMap ProfileFor(WORD vid) {
    PadMap m;
    if (vid == 0x057E) {
        m.a = 1;
        m.b = 2;
        m.x = 3;
        m.y = 4;
    }
    return m;
}

// Optional overrides from rez3d.ini next to this DLL (PadA=2, PadRightX=U, ...)
static void ApplyIniOverrides(PadMap& m) {
    char path[MAX_PATH];
    sprintf_s(path, "%s\\rez3d.ini", g_dir);
    FILE* f = nullptr;
    if (fopen_s(&f, path, "r") != 0 || !f) {
        // Area X: this DLL sits in AX\REZAX\Binaries\Win64, its settings with the
        // runtime in AX\Engine\Binaries\ThirdParty\OpenVR\OpenVRv1_0_2\Win64
        sprintf_s(path, "%s\\..\\..\\..\\Engine\\Binaries\\ThirdParty\\OpenVR\\OpenVRv1_0_2\\Win64\\rez3d.ini", g_dir);
        if (fopen_s(&f, path, "r") != 0 || !f) return;
    }
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char key[64] = {}, val[64] = {};
        if (sscanf_s(line, " %63[^= \t] = %63s", key, (unsigned)sizeof(key), val, (unsigned)sizeof(val)) != 2) continue;
        if (_stricmp(key, "ReticleKeepInView") == 0) m.keepInView = atoi(val) != 0;
        if (_strnicmp(key, "Pad", 3) != 0) continue;
        const char* k = key + 3;
        int n = atoi(val);
        struct { const char* name; int* field; } buttons[] = {
            { "A", &m.a }, { "B", &m.b }, { "X", &m.x }, { "Y", &m.y }, { "LB", &m.lb }, { "RB", &m.rb },
            { "LT", &m.lt }, { "RT", &m.rt }, { "Back", &m.back }, { "Start", &m.start }, { "LS", &m.ls }, { "RS", &m.rs } };
        for (auto& bt : buttons)
            if (_stricmp(k, bt.name) == 0) *bt.field = n;
        if (_stricmp(k, "RightX") == 0) m.rightX = (char)toupper((unsigned char)val[0]);
        if (_stricmp(k, "RightY") == 0) m.rightY = (char)toupper((unsigned char)val[0]);
        if (_stricmp(k, "Deadzone") == 0) m.deadzone = (float)atof(val) / 100.0f;
        if (_stricmp(k, "StickSpeed") == 0) m.stickSpeed = (float)atof(val) / 100.0f;
        if (_stricmp(k, "StickCurve") == 0) m.stickCurve = (float)atof(val);
        if (_stricmp(k, "GameDeadzone") == 0) m.gameDeadzone = (float)atof(val) / 100.0f;
        if (_stricmp(k, "FireOnRB") == 0) m.fireOnRB = n != 0;
        if (_stricmp(k, "Recenter") == 0) m.recenter = n != 0;
        if (_stricmp(k, "RezSpeed") == 0) m.rezSpeed = isdigit((unsigned char)val[0]) ? n : -1;
        if (_stricmp(k, "Bridge") == 0) m.enabled = n != 0;
        if (_stricmp(k, "ReticleLog") == 0) m.reticleLog = n != 0;
    }
    fclose(f);
}

struct BridgeDevice {
    int id = -1;
    JOYCAPSW caps{};
    PadMap map;
    DWORD nextScan = 0;
};
static BridgeDevice g_joy;

// Rescans every 2 s so pads plugged in mid-game are picked up. Microsoft (Xbox)
// pads are skipped: they already work through real XInput.
static void ScanJoysticks() {
    DWORD now = GetTickCount();
    if (g_joy.id >= 0 || (int)(now - g_joy.nextScan) < 0) return;
    g_joy.nextScan = now + 2000;
    UINT count = joyGetNumDevs();
    for (UINT i = 0; i < count && i < 16; i++) {
        JOYINFOEX info{};
        info.dwSize = sizeof(info);
        info.dwFlags = JOY_RETURNALL;
        if (joyGetPosEx(i, &info) != JOYERR_NOERROR) continue;
        JOYCAPSW caps{};
        if (joyGetDevCapsW(i, &caps, sizeof(caps)) != JOYERR_NOERROR) continue;
        if (caps.wMid == 0x045E) continue;
        g_joy.id = (int)i;
        g_joy.caps = caps;
        g_joy.map = ProfileFor(caps.wMid);
        ApplyIniOverrides(g_joy.map);
        Log("bridge: stick speed %.0f%% curve %.2f deadzone %.0f%%", g_joy.map.stickSpeed * 100.0f,
            g_joy.map.stickCurve, g_joy.map.deadzone * 100.0f);
        char name[64] = {};
        WideCharToMultiByte(CP_UTF8, 0, caps.szPname, -1, name, sizeof(name), nullptr, nullptr);
        Log("bridge: joystick %u VID=%04X PID=%04X buttons=%u axes=%u pov=%d '%s'%s", i, caps.wMid, caps.wPid,
            caps.wNumButtons, caps.wNumAxes, (caps.wCaps & JOYCAPS_HASPOV) ? 1 : 0, name,
            g_joy.map.enabled ? "" : " (disabled by PadBridge=0)");
        return;
    }
}

// Speed and curve on a -1..1 stick value, past our small centring dead zone.
// The aim stick starts at the game's own dead zone: Rez ignores tilts below ~24 %
// of the range, and it saw speed x tilt^curve, so most of the stick did nothing
// (speed 60 %, curve 2: no movement below 63 % tilt). Now speed and curve shape
// only the range the game responds to, and the smallest tilt already moves.
static float ShapeStick(float n, const PadMap& m, bool aim) {
    float a = fabsf(n);
    if (a <= m.deadzone) return 0;  // centring noise
    a = (a - m.deadzone) / (1.0f - m.deadzone);
    if (a > 1.0f) a = 1.0f;
    if (m.stickCurve != 1.0f && m.stickCurve > 0.0f) a = powf(a, m.stickCurve);
    a *= m.stickSpeed;
    if (aim && m.gameDeadzone > 0.0f) a = m.gameDeadzone + (1.0f - m.gameDeadzone) * a;
    if (a > 1.0f) a = 1.0f;
    return n < 0 ? -a : a;
}

static short AxisToThumb(DWORD v, UINT mn, UINT mx, const PadMap& m, bool invert, bool aim) {
    if (mx <= mn) return 0;
    float n = ((float)v - (float)mn) / (float)(mx - mn) * 2.0f - 1.0f;
    if (invert) n = -n;
    return (short)(ShapeStick(n, m, aim) * 32767.0f);
}

// The right bumper fires (A) when PadFireOnRB is set
static void ApplyFireOnRB(XINPUT_GAMEPAD& g, const PadMap& m) {
    if (m.fireOnRB && (g.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER))
        g.wButtons = (WORD)((g.wButtons & ~XINPUT_GAMEPAD_RIGHT_SHOULDER) | XINPUT_GAMEPAD_A);
}

// Real XInput pads get the same speed / curve / dead-zone handling
static void ShapeRealThumbs(XINPUT_GAMEPAD& g) {
    static PadMap cfg;
    static bool loaded = false;
    if (!loaded) {
        ApplyIniOverrides(cfg);
        loaded = true;
    }
    g.sThumbLX = (SHORT)(ShapeStick(g.sThumbLX / 32767.0f, cfg, true) * 32767.0f);
    g.sThumbLY = (SHORT)(ShapeStick(g.sThumbLY / 32767.0f, cfg, true) * 32767.0f);
    g.sThumbRX = (SHORT)(ShapeStick(g.sThumbRX / 32767.0f, cfg, false) * 32767.0f);
    g.sThumbRY = (SHORT)(ShapeStick(g.sThumbRY / 32767.0f, cfg, false) * 32767.0f);
    ApplyFireOnRB(g, cfg);
}

static bool ReadBridge(XINPUT_GAMEPAD& g) {
    ScanJoysticks();
    if (g_joy.id < 0 || !g_joy.map.enabled) return false;
    JOYINFOEX info{};
    info.dwSize = sizeof(info);
    info.dwFlags = JOY_RETURNALL | JOY_RETURNPOVCTS;
    if (joyGetPosEx((UINT)g_joy.id, &info) != JOYERR_NOERROR) {
        Log("bridge: joystick %d disconnected", g_joy.id);
        g_joy.id = -1;
        return false;
    }
    const PadMap& m = g_joy.map;
    const JOYCAPSW& c = g_joy.caps;
    auto pressed = [&](int n) { return n > 0 && n <= 32 && (info.dwButtons & (1u << (n - 1))) != 0; };
    ZeroMemory(&g, sizeof(g));
    if (pressed(m.a)) g.wButtons |= XINPUT_GAMEPAD_A;
    if (pressed(m.b)) g.wButtons |= XINPUT_GAMEPAD_B;
    if (pressed(m.x)) g.wButtons |= XINPUT_GAMEPAD_X;
    if (pressed(m.y)) g.wButtons |= XINPUT_GAMEPAD_Y;
    if (pressed(m.lb)) g.wButtons |= XINPUT_GAMEPAD_LEFT_SHOULDER;
    if (pressed(m.rb)) g.wButtons |= XINPUT_GAMEPAD_RIGHT_SHOULDER;
    if (pressed(m.back)) g.wButtons |= XINPUT_GAMEPAD_BACK;
    if (pressed(m.start)) g.wButtons |= XINPUT_GAMEPAD_START;
    if (pressed(m.ls)) g.wButtons |= XINPUT_GAMEPAD_LEFT_THUMB;
    if (pressed(m.rs)) g.wButtons |= XINPUT_GAMEPAD_RIGHT_THUMB;
    g.bLeftTrigger = pressed(m.lt) ? 255 : 0;
    g.bRightTrigger = pressed(m.rt) ? 255 : 0;

    if ((c.wCaps & JOYCAPS_HASPOV) && LOWORD(info.dwPOV) != JOY_POVCENTERED) {
        DWORD pov = info.dwPOV;  // hundredths of a degree, 0 = up, clockwise
        if (pov >= 31500 || pov <= 4500) g.wButtons |= XINPUT_GAMEPAD_DPAD_UP;
        if (pov >= 4500 && pov <= 13500) g.wButtons |= XINPUT_GAMEPAD_DPAD_RIGHT;
        if (pov >= 13500 && pov <= 22500) g.wButtons |= XINPUT_GAMEPAD_DPAD_DOWN;
        if (pov >= 22500 && pov <= 31500) g.wButtons |= XINPUT_GAMEPAD_DPAD_LEFT;
    }

    // Joystick Y grows downward, XInput Y upward
    g.sThumbLX = AxisToThumb(info.dwXpos, c.wXmin, c.wXmax, m, false, true);
    g.sThumbLY = AxisToThumb(info.dwYpos, c.wYmin, c.wYmax, m, true, true);
    auto axis = [&](char name, bool invert) -> short {
        switch (name) {
        case 'X': return AxisToThumb(info.dwXpos, c.wXmin, c.wXmax, m, invert, false);
        case 'Y': return AxisToThumb(info.dwYpos, c.wYmin, c.wYmax, m, invert, false);
        case 'Z': return (c.wCaps & JOYCAPS_HASZ) ? AxisToThumb(info.dwZpos, c.wZmin, c.wZmax, m, invert, false) : 0;
        case 'R': return (c.wCaps & JOYCAPS_HASR) ? AxisToThumb(info.dwRpos, c.wRmin, c.wRmax, m, invert, false) : 0;
        case 'U': return (c.wCaps & JOYCAPS_HASU) ? AxisToThumb(info.dwUpos, c.wUmin, c.wUmax, m, invert, false) : 0;
        case 'V': return (c.wCaps & JOYCAPS_HASV) ? AxisToThumb(info.dwVpos, c.wVmin, c.wVmax, m, invert, false) : 0;
        default: return 0;
        }
    };
    g.sThumbRX = axis(m.rightX, false);
    g.sThumbRY = axis(m.rightY, true);
    ApplyFireOnRB(g, m);

    static WORD lastButtons = 0xFFFF;
    if (g.wButtons != lastButtons) {
        lastButtons = g.wButtons;
        static unsigned logged = 0;
        if (logged++ < 40) Log("bridge: buttons=0x%04X (raw 0x%08lX pov %lu)", g.wButtons, info.dwButtons, info.dwPOV);
    }
    return true;
}

static DWORD GetPadState(DWORD index, XINPUT_STATE* state) {
    EnsureInit();
    if (!state) return ERROR_BAD_ARGUMENTS;
    EnsureReal();
    PFN_GetState real = g_real ? (PFN_GetState)GetProcAddress(g_real, "XInputGetState") : nullptr;
    DWORD result = real ? real(index, state) : ERROR_DEVICE_NOT_CONNECTED;

    static LONG s_firstCalls = 0;
    if (InterlockedIncrement(&s_firstCalls) <= 4) Log("XInputGetState(%lu) real=%lu", index, result);
    {
        // How often the game polls pad 0 (first minute): tells "not polled" from "ignored"
        static LONG s_polls = 0, s_reports = 0;
        static DWORD s_since = 0;
        if (index == 0) InterlockedIncrement(&s_polls);
        DWORD t = GetTickCount();
        if (!s_since) s_since = t;
        if (t - s_since >= 5000 && s_reports < 12) {
            s_reports++;
            Log("pad 0 polled %ld times in %.1f s", InterlockedExchange(&s_polls, 0), (t - s_since) / 1000.0);
            s_since = t;
        }
    }

    unsigned short vb = VirtualButtons();
    short lx, ly, rx, ry;
    VirtualSticks(lx, ly, rx, ry);
    bool anyVirtual = vb != 0 || lx || ly || rx || ry;

    if (index != 0) return result;

    // Every change of the virtual sticks, with its time (reticle tests)
    static short lastV[4] = {};
    if (lx != lastV[0] || ly != lastV[1] || rx != lastV[2] || ry != lastV[3]) {
        lastV[0] = lx; lastV[1] = ly; lastV[2] = rx; lastV[3] = ry;
        Log("virtual sticks: %d,%d,%d,%d", lx, ly, rx, ry);
    }

    static DWORD s_packet = 0;
    if (result != ERROR_SUCCESS) {
        // No XInput pad: use a bridged DirectInput/HID pad if there is one,
        // with the keyboard / rezpad.txt layered on top
        XINPUT_GAMEPAD pad{};
        bool bridged = ReadBridge(pad);
        ZeroMemory(state, sizeof(*state));
        state->dwPacketNumber = ++s_packet;
        if (bridged) {
            state->Gamepad = pad;
            state->Gamepad.wButtons |= vb;
            if (lx) state->Gamepad.sThumbLX = lx;
            if (ly) state->Gamepad.sThumbLY = ly;
            if (rx) state->Gamepad.sThumbRX = rx;
            if (ry) state->Gamepad.sThumbRY = ry;
            return ERROR_SUCCESS;
        }
        state->Gamepad.wButtons = vb;
        state->Gamepad.sThumbLX = lx;
        state->Gamepad.sThumbLY = ly;
        state->Gamepad.sThumbRX = rx;
        state->Gamepad.sThumbRY = ry;
        result = ERROR_SUCCESS;
    } else {
        ShapeRealThumbs(state->Gamepad);
        if (anyVirtual) {
            state->Gamepad.wButtons |= vb;
            if (lx) state->Gamepad.sThumbLX = lx;
            if (ly) state->Gamepad.sThumbLY = ly;
            if (rx) state->Gamepad.sThumbRX = rx;
            if (ry) state->Gamepad.sThumbRY = ry;
            state->dwPacketNumber = ++s_packet;
        }
    }

    static DWORD lastLog = 0;
    DWORD now = GetTickCount();
    if (anyVirtual && now - lastLog > 500) {
        lastLog = now;
        Log("virtual pad: buttons=0x%04X sticks=%d,%d,%d,%d", vb, lx, ly, rx, ry);
    }
    return result;
}

// ---------------------------------------------------------------------------
// L3 recenter (Rez)
// ---------------------------------------------------------------------------
// Rez moves its reticle from the left stick on a fixed 60 Hz tick, each axis on its
// own, and stops it at fixed edges (about 50 deg across and 43 deg up/down from the
// middle of the view). The speed comes from Rez's own "gamepad speed" option
// (pause > controls), saved as a 32-bit value at 0x28 in CFG\rez-infinite.cfg.
// So a recenter needs no tracking: push the stick into the bottom-left corner long
// enough to get there from anywhere, then back at full tilt for the measured
// edge-to-middle time of each axis. The edge resets the position, so nothing adds up.
// Measured in Area 1 on 2026-11-01 (the same at 60 and ~1000 fps).
namespace recenter {
// Edge-to-middle time at full tilt (seconds) for each gamepad speed value, the mean
// of both edges of each axis (+-1 game tick). Each step adds about 25 % of the
// default speed (76 deg/s); 0 moves like 1, and Rez treats anything above 4 as 4.
struct Timing { float x, y; };
static const Timing kTiming[] = {
    { 0.654f, 0.550f },  // 0
    { 0.654f, 0.550f },  // 1: one bar, the game's default
    { 0.527f, 0.438f },  // 2
    { 0.434f, 0.371f },  // 3
    { 0.375f, 0.319f },  // 4: the fastest
};
const int kMinSpeed = 0, kMaxSpeed = 4, kDefaultSpeed = 1;
const float kMargin = 0.1f;  // extra push, so the corner is reached for sure

static SRWLOCK g_lock = SRWLOCK_INIT;
static int g_enabled = -1;  // -1 = not decided yet
static WORD g_buttons = 0;
static bool g_active = false;
static LARGE_INTEGER g_start{};
static Timing g_t{};

static bool Enabled() {
    if (g_enabled < 0) {
        PadMap m;
        ApplyIniOverrides(m);
        // Area X moves the avatar instead, with its own settings
        char dir[MAX_PATH];
        strcpy_s(dir, g_dir);
        _strupr_s(dir);
        g_enabled = m.recenter && !strstr(dir, "\\REZAX\\") ? 1 : 0;
        Log("recenter: L3 %s", g_enabled ? "on" : "off");
    }
    return g_enabled == 1;
}

// Rez's gamepad speed: PadRezSpeed in rez3d.ini if set, else Rez's own options file
// (written when the game quits), else the game's default
static int GameSpeed() {
    PadMap m;
    ApplyIniOverrides(m);
    int v = -1;
    char src[MAX_PATH + 64];
    if (m.rezSpeed >= kMinSpeed && m.rezSpeed <= kMaxSpeed) {
        v = m.rezSpeed;
        strcpy_s(src, "PadRezSpeed in rez3d.ini");
    } else {
        char path[MAX_PATH];
        sprintf_s(path, "%s\\..\\CFG\\rez-infinite.cfg", g_dir);
        FILE* f = nullptr;
        unsigned char cfg[424];
        if (fopen_s(&f, path, "rb") == 0 && f) {
            const size_t n = fread(cfg, 1, sizeof(cfg), f);
            const bool exact = n == sizeof(cfg) && fgetc(f) == EOF;
            fclose(f);
            const int raw = cfg[0x28] | cfg[0x29] << 8 | cfg[0x2A] << 16 | cfg[0x2B] << 24;
            if (exact && raw >= kMinSpeed && raw <= kMaxSpeed) {
                v = raw;
                strcpy_s(src, "CFG\\rez-infinite.cfg");
            } else {
                sprintf_s(src, "the default: CFG\\rez-infinite.cfg holds %d (%s)", raw,
                          exact ? "out of range" : "unexpected size");
            }
        } else {
            strcpy_s(src, "the default: CFG\\rez-infinite.cfg not found");
        }
    }
    if (v < 0) v = kDefaultSpeed;
    static int logged = -2;
    if (v != logged) {
        logged = v;
        Log("recenter: gamepad speed %d from %s", v, src);
    }
    return v;
}

static float Since(const LARGE_INTEGER& t) {
    LARGE_INTEGER now, freq;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&freq);
    return (float)((double)(now.QuadPart - t.QuadPart) / (double)freq.QuadPart);
}

static void Apply(XINPUT_GAMEPAD& g) {
    if (!Enabled()) return;
    AcquireSRWLockExclusive(&g_lock);
    const WORD pressed = (WORD)(g.wButtons & ~g_buttons);
    g_buttons = g.wButtons;
    if (!g_active && (pressed & XINPUT_GAMEPAD_LEFT_THUMB)) {
        g_t = kTiming[GameSpeed()];
        g_active = true;
        QueryPerformanceCounter(&g_start);
        Log("recenter: start (corner %.2f s, back %.3f / %.3f s)", 2.0f * g_t.x + kMargin, g_t.x, g_t.y);
    }
    if (g_active) {
        const float push = 2.0f * (g_t.x > g_t.y ? g_t.x : g_t.y) + kMargin;
        const float t = Since(g_start);
        if (t < push) {
            g.sThumbLX = -32767;
            g.sThumbLY = -32767;
        } else {
            g.sThumbLX = t < push + g_t.x ? 32767 : 0;
            g.sThumbLY = t < push + g_t.y ? 32767 : 0;
            if (t >= push + g_t.x && t >= push + g_t.y) {
                g_active = false;
                Log("recenter: done");
            }
        }
    }
    g.wButtons &= (WORD)~XINPUT_GAMEPAD_LEFT_THUMB;
    ReleaseSRWLockExclusive(&g_lock);
}
}  // namespace recenter

// ---------------------------------------------------------------------------
// Keep the reticle in view (Rez)
// ---------------------------------------------------------------------------
// Rez lets its reticle go about 50 deg across and 43 deg up/down from the middle of the
// view, wider than most fields of view, and it stays wherever it was left. The 3D runtime
// measures where the reticle is shown in each eye (openvr_api.dll's export
// Rez3DReticleState, read back about 1 ms after the draw), and this stops the aim stick
// before the reticle leaves the picture: outward tilt is capped by the room left to the
// edge, so it brakes over the last stretch and stops with the whole octagon in view. Rez
// reacts to the stick on its next 60 Hz tick, so the cap leaves a few frames of travel
// (kBrake, kLag). Its reticle also drifts on one axis as it moves along the other, so one
// that ends up past an edge anyway gets a gentle push back in (kReturn), and slides along
// the edge. Off during an L3 recenter, and when the reticle is not being drawn (menus,
// pause, Area X).
namespace inview {
const double kFresh = 0.1;       // only a reticle drawn this recently counts (seconds)
const float kEdge = 0.98f;       // keep the octagon inside this share of the picture
const float kBrake = 0.06f;      // outward speed is capped at room / kBrake (seconds)
const float kGainStart = 4.0f;   // picture half-widths per second at full tilt, until measured
const float kLag = 0.04f;        // seconds it keeps travelling after the stick changes (a tick and a frame)
const float kReturn = 0.3f;      // the most tilt used to push it back in (past the dead zone)

static int g_mode = -1;  // -1 = not decided yet, 0 = off, 1 = on
static bool g_gate = false;
static bool g_log = false;
static float g_dz = 0.24f;  // the game's own dead zone (PadGameDeadzone)
static Rez3DReticleStateFn g_state = nullptr;
static int g_lastSeq = -1;
// Measured speed per unit of tilt past the dead zone, per axis, learned while it moves
static float g_gain[2] = { kGainStart, kGainStart };
static float g_sent[2] = {};  // the tilt (past the dead zone, signed) sent last
static float g_vel[2] = {};   // its speed over the last 30 ms or more (picture half-widths per second)
static bool g_anchor = false;
static double g_anchorT = 0;
static float g_anchorP[2] = {};
static int g_braking[2] = {};  // 0 free, 1 braking, 2 pushing back in (for the log)

static bool Enabled() {
    if (g_mode < 0) {
        PadMap m;
        ApplyIniOverrides(m);
        char dir[MAX_PATH];
        strcpy_s(dir, g_dir);
        _strupr_s(dir);
        const bool rez = !strstr(dir, "\\REZAX\\");
        g_gate = m.keepInView && rez;
        g_log = m.reticleLog && rez;
        g_dz = m.gameDeadzone;
        g_mode = g_gate || g_log ? 1 : 0;
        Log("keep in view: %s%s", g_gate ? "on" : "off", g_log ? " (logging the reticle)" : "");
    }
    if (g_mode == 1 && !g_state) {
        HMODULE vr = GetModuleHandleW(L"openvr_api.dll");
        if (vr) g_state = (Rez3DReticleStateFn)GetProcAddress(vr, "Rez3DReticleState");
    }
    return g_mode == 1 && g_state;
}

static double Now() {
    static LARGE_INTEGER freq{};
    if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return (double)t.QuadPart / (double)freq.QuadPart;
}

// Stick value <-> signed tilt past the game's dead zone (0..1)
static float Tilt(SHORT v) {
    const float a = fabsf(v / 32767.0f);
    float f = a > g_dz ? (a - g_dz) / (1.0f - g_dz) : 0.0f;
    if (f > 1.0f) f = 1.0f;
    return v < 0 ? -f : f;
}
static SHORT FromTilt(float f) {
    if (f == 0.0f) return 0;
    const float a = g_dz + (1.0f - g_dz) * fabsf(f);
    return (SHORT)((f < 0 ? -a : a) * 32767.0f);
}

// While the reticle moves sideways, an occasional single reading is of another, about twice
// as wide quad drawn like the octagon: skip a reading that suddenly grows that much over the
// one before (the octagon itself only grows gradually, toward the edges)
static bool Plausible(const Rez3DReticleState& s) {
    static float last[2] = {};
    const float hx = (std::max)(s.halfX[0], s.halfX[1]), hy = (std::max)(s.halfY[0], s.halfY[1]);
    const bool jump = last[0] > 0.0f && (hx > 1.4f * last[0] || hy > 1.4f * last[1]);
    last[0] = hx;
    last[1] = hy;
    return !jump;
}

static void Apply(XINPUT_GAMEPAD& g) {
    if (!Enabled()) return;
    static Rez3DReticleState s{};  // the latest plausible reading
    Rez3DReticleState now{};
    const bool have = g_state(&now) && now.version == kRez3DReticleStateVersion && now.drawnAgo <= kFresh;
    bool fresh = have && now.sequence != g_lastSeq;
    if (fresh) {
        g_lastSeq = now.sequence;
        if (g_log)
            Log("reticle: x %+.4f %+.4f y %+.4f %+.4f half %.4f %.4f drawn %.1f ms ago | stick %d,%d", now.x[0],
                now.x[1], now.y[0], now.y[1], now.halfX[0], now.halfY[0], now.drawnAgo * 1000.0, g.sThumbLX,
                g.sThumbLY);
        fresh = Plausible(now);
        if (fresh) s = now;
    }
    if (!have || !s.version || !g_gate || recenter::g_active) {
        g_anchor = false;
        g_vel[0] = g_vel[1] = 0.0f;
        g_sent[0] = Tilt(g.sThumbLX);
        g_sent[1] = Tilt(g.sThumbLY);
        return;
    }
    const float p[2] = { 0.5f * (s.x[0] + s.x[1]), 0.5f * (s.y[0] + s.y[1]) };
    // How fast it moves per unit of tilt: from its travel over at least 30 ms of steady
    // outward-or-inward tilt, inside the picture (Rez's own stops are outside it)
    if (fresh) {
        const double t = Now();
        if (!g_anchor) {
            g_anchor = true;
            g_anchorT = t;
            g_anchorP[0] = p[0];
            g_anchorP[1] = p[1];
        } else if (t - g_anchorT >= 0.03) {
            for (int a = 0; a < 2; a++) {
                const float v = (p[a] - g_anchorP[a]) / (float)(t - g_anchorT);
                const float f = g_sent[a];
                if (fabsf(f) >= 0.25f && v * f > 0.0f && fabsf(p[a]) < 1.0f) {
                    const float seen = fabsf(v / f);
                    g_gain[a] = seen > g_gain[a] ? seen : 0.9f * g_gain[a] + 0.1f * seen;
                }
                g_vel[a] = v;
                g_anchorP[a] = p[a];
            }
            g_anchorT = t;
        }
    }
    // Room to the edge each way, for the eye whose octagon is nearest to it
    const float right = (std::max)(s.x[0] + s.halfX[0], s.x[1] + s.halfX[1]);
    const float left = (std::min)(s.x[0] - s.halfX[0], s.x[1] - s.halfX[1]);
    const float top = (std::max)(s.y[0] + s.halfY[0], s.y[1] + s.halfY[1]);
    const float bottom = (std::min)(s.y[0] - s.halfY[0], s.y[1] - s.halfY[1]);
    SHORT* stick[2] = { &g.sThumbLX, &g.sThumbLY };
    const float roomPlus[2] = { kEdge - right, kEdge - top };
    const float roomMinus[2] = { left + kEdge, bottom + kEdge };
    static const char* kAxis[2] = { "x", "y" };
    for (int a = 0; a < 2; a++) {
        const float f = Tilt(*stick[a]);
        float out = f;
        // Outward: capped by the room left, less what it is still travelling (it speeds up
        // toward the side edges, faster than the speed learned further in)
        const float room = f > 0 ? roomPlus[a] : roomMinus[a];
        const float coming = f != 0.0f ? (std::max)(0.0f, (f > 0 ? g_vel[a] : -g_vel[a]) * kLag) : 0.0f;
        float cap = room - coming > 0.0f ? (room - coming) / (g_gain[a] * kBrake) : 0.0f;
        if (cap < 0.02f) cap = 0.0f;  // below the smallest step: stop
        if (f != 0.0f && fabsf(f) > cap) out = f > 0 ? cap : -cap;
        // Past an edge (an overshoot, or pulled out sideways: the reticle drifts on the other
        // axis as it moves): push gently back in, unless already going back faster
        const bool overPlus = roomPlus[a] < 0.0f, overMinus = roomMinus[a] < 0.0f;
        if (overPlus != overMinus) {
            const float over = overPlus ? -roomPlus[a] : -roomMinus[a];
            float back = (std::min)(kReturn, over / (g_gain[a] * kBrake));
            if (overPlus) back = -back;
            if (back < 0.0f ? out > back : out < back) out = back;
        }
        if (out != f) *stick[a] = FromTilt(out);
        const int state = out == f ? 0 : (out * f <= 0.0f && out != 0.0f) ? 2 : 1;
        if (state != g_braking[a] && g_log)
            Log("keep in view: %s %s (room %+.3f %+.3f, speed %+.2f, gain %.2f)", kAxis[a],
                state == 0 ? "free" : state == 1 ? "braking" : "pushing back in", roomMinus[a], roomPlus[a], g_vel[a],
                g_gain[a]);
        g_braking[a] = state;
        g_sent[a] = Tilt(*stick[a]);
    }
}
}  // namespace inview

extern "C" DWORD WINAPI XInputGetState(DWORD index, XINPUT_STATE* state) noexcept {
    const DWORD r = GetPadState(index, state);
    if (index == 0 && r == ERROR_SUCCESS && state) {
        recenter::Apply(state->Gamepad);
        inview::Apply(state->Gamepad);
    }
    return r;
}

extern "C" DWORD WINAPI XInputSetState(DWORD index, XINPUT_VIBRATION* vibration) noexcept {
    EnsureInit();
    EnsureReal();
    PFN_SetState real = g_real ? (PFN_SetState)GetProcAddress(g_real, "XInputSetState") : nullptr;
    if (real) return real(index, vibration);
    return ERROR_SUCCESS;
}

extern "C" DWORD WINAPI XInputGetCapabilities(DWORD index, DWORD flags, XINPUT_CAPABILITIES* caps) noexcept {
    EnsureInit();
    if (!caps) return ERROR_BAD_ARGUMENTS;
    EnsureReal();
    PFN_GetCaps real = g_real ? (PFN_GetCaps)GetProcAddress(g_real, "XInputGetCapabilities") : nullptr;
    if (real) {
        DWORD r = real(index, flags, caps);
        if (r == ERROR_SUCCESS) return r;
    }
    ZeroMemory(caps, sizeof(*caps));
    caps->Type = XINPUT_DEVTYPE_GAMEPAD;
    caps->SubType = 0x01; // XINPUT_DEVICE_SUBTYPE_GAMEPAD
    caps->Flags = 0;
    caps->Gamepad.wButtons = 0xF3FF;
    caps->Gamepad.bLeftTrigger = 255;
    caps->Gamepad.bRightTrigger = 255;
    caps->Gamepad.sThumbLX = 32767;
    caps->Gamepad.sThumbLY = 32767;
    caps->Gamepad.sThumbRX = 32767;
    caps->Gamepad.sThumbRY = 32767;
    caps->Vibration.wLeftMotorSpeed = 65535;
    caps->Vibration.wRightMotorSpeed = 65535;
    return ERROR_SUCCESS;
}

extern "C" void WINAPI XInputEnable(BOOL enable) noexcept {
    EnsureInit();
    EnsureReal();
    PFN_Enable real = g_real ? (PFN_Enable)GetProcAddress(g_real, "XInputEnable") : nullptr;
    if (real) real(enable);
}

extern "C" DWORD WINAPI XInputGetBatteryInformation(DWORD index, BYTE type, XINPUT_BATTERY_INFORMATION* battery) noexcept {
    EnsureInit();
    if (!battery) return ERROR_BAD_ARGUMENTS;
    EnsureReal();
    PFN_GetBattery real = g_real ? (PFN_GetBattery)GetProcAddress(g_real, "XInputGetBatteryInformation") : nullptr;
    if (real) {
        DWORD r = real(index, type, battery);
        if (r == ERROR_SUCCESS) return r;
    }
    ZeroMemory(battery, sizeof(*battery));
    battery->BatteryType = BATTERY_TYPE_WIRED;
    battery->BatteryLevel = BATTERY_LEVEL_FULL;
    return ERROR_SUCCESS;
}

extern "C" DWORD WINAPI XInputGetKeystroke(DWORD index, DWORD flags, PXINPUT_KEYSTROKE keystroke) noexcept {
    EnsureInit();
    EnsureReal();
    PFN_GetKeystroke real = g_real ? (PFN_GetKeystroke)GetProcAddress(g_real, "XInputGetKeystroke") : nullptr;
    if (real) return real(index, flags, keystroke);
    return ERROR_EMPTY;
}

extern "C" DWORD WINAPI XInputGetDSoundAudioDeviceGuids(DWORD index, GUID* render, GUID* capture) noexcept {
    EnsureInit();
    if (render) ZeroMemory(render, sizeof(GUID));
    if (capture) ZeroMemory(capture, sizeof(GUID));
    return ERROR_SUCCESS;
}

extern "C" DWORD WINAPI XInputGetAudioDeviceIds(DWORD index, LPWSTR render, UINT* renderCount, LPWSTR capture,
                                                UINT* captureCount) noexcept {
    EnsureInit();
    if (renderCount) *renderCount = 0;
    if (captureCount) *captureCount = 0;
    return ERROR_SUCCESS;
}

extern "C" DWORD WINAPI XInputGetStateEx(DWORD index, XINPUT_STATE* state) noexcept {
    return XInputGetState(index, state);
}

extern "C" DWORD WINAPI XInputWaitForGuideButton(DWORD index, DWORD flags, LPVOID data) noexcept {
    return ERROR_SUCCESS;
}
extern "C" DWORD WINAPI XInputCancelGuideButtonWait(DWORD index) noexcept { return ERROR_SUCCESS; }
extern "C" DWORD WINAPI XInputPowerOffController(DWORD index) noexcept { return ERROR_SUCCESS; }

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = module;
        DisableThreadLibraryCalls(module);
        EnsureInit();
        Log("xinput proxy loaded (module=%p)", module);
    }
    return TRUE;
}
