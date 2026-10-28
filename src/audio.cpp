// Mutes or unmutes the game's own audio session (what the Windows volume mixer
// shows for REZ.exe). Windows remembers per-app mutes, so a mute made during a
// test run, or in the mixer, would otherwise stick: normal launches unmute,
// hidden test runs mute.

#include "audio.h"
#include "log.h"

#include <windows.h>
#include <audiopolicy.h>
#include <mmdeviceapi.h>

#include <thread>

template <typename T> static void Release(T*& p) {
    if (p) {
        p->Release();
        p = nullptr;
    }
}

// Returns how many of this process's sessions were found (on any output device)
static int ApplyMute(bool mute) {
    const DWORD pid = GetCurrentProcessId();
    int found = 0;
    IMMDeviceEnumerator* en = nullptr;
    IMMDeviceCollection* devs = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                (void**)&en)) ||
        FAILED(en->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &devs))) {
        Release(en);
        return 0;
    }
    UINT count = 0;
    devs->GetCount(&count);
    for (UINT i = 0; i < count; i++) {
        IMMDevice* dev = nullptr;
        IAudioSessionManager2* mgr = nullptr;
        IAudioSessionEnumerator* sessions = nullptr;
        if (SUCCEEDED(devs->Item(i, &dev)) &&
            SUCCEEDED(dev->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, (void**)&mgr)) &&
            SUCCEEDED(mgr->GetSessionEnumerator(&sessions))) {
            int n = 0;
            sessions->GetCount(&n);
            for (int k = 0; k < n; k++) {
                IAudioSessionControl* ctl = nullptr;
                IAudioSessionControl2* ctl2 = nullptr;
                ISimpleAudioVolume* vol = nullptr;
                DWORD spid = 0;
                if (SUCCEEDED(sessions->GetSession(k, &ctl)) &&
                    SUCCEEDED(ctl->QueryInterface(__uuidof(IAudioSessionControl2), (void**)&ctl2)) &&
                    SUCCEEDED(ctl2->GetProcessId(&spid)) && spid == pid &&
                    SUCCEEDED(ctl->QueryInterface(__uuidof(ISimpleAudioVolume), (void**)&vol))) {
                    BOOL muted = FALSE;
                    vol->GetMute(&muted);
                    if ((muted != FALSE) != mute) {
                        vol->SetMute(mute ? TRUE : FALSE, nullptr);
                        LogPrintf("Audio: %s the game's audio session", mute ? "muted" : "unmuted");
                    }
                    found++;
                }
                Release(vol);
                Release(ctl2);
                Release(ctl);
            }
        }
        Release(sessions);
        Release(mgr);
        Release(dev);
    }
    Release(devs);
    Release(en);
    return found;
}

void StartGameAudioMute(bool mute) {
    std::thread([mute] {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        // The session only exists once the game starts playing sound
        for (int tries = 0; tries < 60; tries++) {
            if (ApplyMute(mute) > 0) break;
            Sleep(500);
        }
        CoUninitialize();
    }).detach();
}
