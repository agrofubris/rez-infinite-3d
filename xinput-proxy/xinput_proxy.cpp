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
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

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

static unsigned short VirtualButtons() {
    unsigned short b = 0;
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
    if (fopen_s(&f, path, "r") != 0 || !f) return;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char key[64] = {}, val[64] = {};
        if (sscanf_s(line, " %63[^= \t] = %63s", key, (unsigned)sizeof(key), val, (unsigned)sizeof(val)) != 2) continue;
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
        if (_stricmp(k, "Bridge") == 0) m.enabled = n != 0;
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

// Speed and curve on a -1..1 stick value. The game applies its own dead zone,
// so values are passed through linearly like a real XInput pad would report
// them (rescaling past our dead zone made small tilts jump to high speeds).
static float ShapeStick(float n, const PadMap& m) {
    float a = fabsf(n);
    if (a > 1.0f) a = 1.0f;
    if (m.stickCurve != 1.0f && m.stickCurve > 0.0f) a = powf(a, m.stickCurve);
    a *= m.stickSpeed;
    if (a > 1.0f) a = 1.0f;
    return n < 0 ? -a : a;
}

static short AxisToThumb(DWORD v, UINT mn, UINT mx, const PadMap& m, bool invert) {
    if (mx <= mn) return 0;
    float n = ((float)v - (float)mn) / (float)(mx - mn) * 2.0f - 1.0f;
    if (invert) n = -n;
    if (fabsf(n) < m.deadzone) return 0;  // only hides centring noise
    return (short)(ShapeStick(n, m) * 32767.0f);
}

// Real XInput pads get the same speed / curve when they are set
static void ShapeRealThumbs(XINPUT_GAMEPAD& g) {
    static PadMap cfg;
    static bool loaded = false;
    if (!loaded) {
        ApplyIniOverrides(cfg);
        loaded = true;
    }
    if (cfg.stickSpeed == 1.0f && cfg.stickCurve == 1.0f) return;
    SHORT* thumbs[] = { &g.sThumbLX, &g.sThumbLY, &g.sThumbRX, &g.sThumbRY };
    for (SHORT* t : thumbs)
        *t = (SHORT)(ShapeStick(*t / 32767.0f, cfg) * 32767.0f);
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
    g.sThumbLX = AxisToThumb(info.dwXpos, c.wXmin, c.wXmax, m, false);
    g.sThumbLY = AxisToThumb(info.dwYpos, c.wYmin, c.wYmax, m, true);
    auto axis = [&](char name, bool invert) -> short {
        switch (name) {
        case 'X': return AxisToThumb(info.dwXpos, c.wXmin, c.wXmax, m, invert);
        case 'Y': return AxisToThumb(info.dwYpos, c.wYmin, c.wYmax, m, invert);
        case 'Z': return (c.wCaps & JOYCAPS_HASZ) ? AxisToThumb(info.dwZpos, c.wZmin, c.wZmax, m, invert) : 0;
        case 'R': return (c.wCaps & JOYCAPS_HASR) ? AxisToThumb(info.dwRpos, c.wRmin, c.wRmax, m, invert) : 0;
        case 'U': return (c.wCaps & JOYCAPS_HASU) ? AxisToThumb(info.dwUpos, c.wUmin, c.wUmax, m, invert) : 0;
        case 'V': return (c.wCaps & JOYCAPS_HASV) ? AxisToThumb(info.dwVpos, c.wVmin, c.wVmax, m, invert) : 0;
        default: return 0;
        }
    };
    g.sThumbRX = axis(m.rightX, false);
    g.sThumbRY = axis(m.rightY, true);

    static WORD lastButtons = 0xFFFF;
    if (g.wButtons != lastButtons) {
        lastButtons = g.wButtons;
        static unsigned logged = 0;
        if (logged++ < 40) Log("bridge: buttons=0x%04X (raw 0x%08lX pov %lu)", g.wButtons, info.dwButtons, info.dwPOV);
    }
    return true;
}

extern "C" DWORD WINAPI XInputGetState(DWORD index, XINPUT_STATE* state) noexcept {
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
