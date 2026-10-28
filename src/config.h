#pragma once

#include <string>

struct StereoConfig {
    std::string format = "SBS";     // SBS, SBS_Cross, TAB, TAB_Cross, Interlaced, Interlaced_Reversed,
                                    // Checkerboard, Mono_Left, Mono_Right
    int monitor = 0;                // 0-based index of display monitor
    int fullscreen = 1;             // borderless fullscreen on chosen monitor
    int vsync = 1;
    // 3D Vision / geo-11 style stereo: separation is the on-screen parallax at
    // infinity in percent of a real eye distance (100% = physically correct),
    // convergence the view depth (meters) that lands on the screen plane.
    float separation = 100.0f;
    float menuSeparation = -1.0f;   // experimental: separation while a menu is detected (-1 = off)
    float convergence = 20.0f;     // the avatar flies ~19 m ahead: put it on the screen plane
    int screenWidthMM = 0;          // physical screen width (0 = from EDID)
    float fovH = 90.0f;             // horizontal FOV; Rez frames the avatar below centre, narrower crops it
    float renderScale = 1.0f;       // eye render resolution relative to output resolution (up to 2 = supersampling)
    float interlaceFilter = 0.5f;   // vertical filter for row/column-decimating formats (0 = off, 1 = softest)
    float crosstalk = 0.0f;         // fraction of the other eye leaking through the glasses (0 = off)
    float crosstalkLift = 0.0f;     // 0-1: raise black so ghosts on black can be cancelled too
    float brightness = 1.0f;        // linear-light gain (glasses absorb about half the light)
    float gamma = 1.0f;             // >1 brightens midtones
    std::string tracking = "off";   // off, mouse
    float mouseSensitivity = 0.25f; // degrees per pixel
    int invertY = 1;
    int fps = 0;                    // reported HMD display frequency (0 = monitor refresh rate)
    int framePacing = 1;            // WaitGetPoses blocks until the previous frame was presented
    int unmuteOnStart = 1;          // unmute the game's audio session on launch (Windows remembers mutes)
    int headless = 0;               // testing: render off-screen, no window or display mode change
    int gpuComposite = 1;           // composite on the GPU via shared textures (0 = CPU readback)
    float nearZ = 0.05f;
    float farZ = 10000.0f;
    int adapter = -1;               // -1 = same adapter as game
    int logFrameCalls = 0;          // verbose per-frame logging
    int dumpFrames = 0;             // save N composited frames as BMP (debug)
    int dumpAfter = 0;              // skip this many frames before dumping
    int dumpEvery = 1;              // dump one frame every N frames
    int focusEvent = 400;           // OpenVR focus event type pushed at init (0=off)
};

// Loads rez3d.ini from the DLL directory. Missing file -> defaults.
bool LoadConfig(const char* dllDir, StereoConfig& cfg);

// Writes Separation/Convergence back into rez3d.ini, keeping everything else.
bool SaveStereoSettings(const char* dllDir, float separation, float convergence, float crosstalk);
