#pragma once

#include <string>

struct StereoConfig {
    std::string format = "SBS";     // SBS, SBS_Cross, TAB, TAB_Cross, Interlaced, Interlaced_Reversed,
                                    // Checkerboard, Mono_Left, Mono_Right, LeiaSR
    std::string leiaWindow = "top"; // LeiaSR: window handed to the weaver: top (the game's) or own
    int monitor = 0;                // 0-based index of display monitor
    int fullscreen = 1;             // borderless fullscreen on chosen monitor
    int vsync = 1;
    // 3D Vision / geo-11 style stereo: separation is the on-screen parallax at
    // infinity in percent of a real eye distance (100% = physically correct),
    // convergence the view depth (meters) that lands on the screen plane.
    float separation = 100.0f;
    float menuSeparation = -1.0f;   // experimental: separation while a menu is detected (-1 = off)
    float convergence = 20.0f;     // the avatar flies ~19 m ahead: put it on the screen plane
    // Area X is built at a closer scale (crosshair ~3 m, avatar ~9 m ahead) and reads
    // its own key, AreaXConvergence, so one ini can serve both games.
    bool areaX = false;             // running inside Area X (REZAX)
    float areaXConvergence = 3.0f;  // puts the crosshair on the screen plane
    int screenWidthMM = 0;          // physical screen width (0 = from EDID)
    float fovH = 85.0f;             // horizontal FOV (with LensShift 7 it frames like the original)
    float renderScale = 1.0f;       // eye render resolution relative to output resolution (up to 2 = supersampling)
    float interlaceFilter = 0.5f;   // vertical filter for row/column-decimating formats (0 = off, 1 = softest)
    float crosstalk = 0.0f;         // fraction of the other eye leaking through the glasses (0 = off)
    float crosstalkLift = 0.0f;     // 0-1: raise black so ghosts on black can be cancelled too
    float brightness = 1.0f;        // linear-light gain (glasses absorb about half the light)
    float gamma = 1.0f;             // >1 brightens midtones
    std::string tracking = "off";   // off, mouse
    float mouseSensitivity = 0.25f; // degrees per pixel
    int invertY = 1;
    // Framing: a vertical lens shift with a level head. The default puts the
    // Area 1 avatar where the non-VR versions have it. A shift (unlike pitching
    // the head) keeps menus and warning panels rectangular; they move with it.
    float lensShift = 7.0f;         // percent of the picture height the image moves down (0 = VR mode's)
    float cameraBack = 0.0f;        // meters behind the default head position
    float cameraUp = 0.0f;          // meters above it
    float dumpAfterSec = 0.0f;      // testing: time-based frame dumps (0 = use DumpAfter frames)
    float dumpEverySec = 1.0f;
    // Follow aim: the virtual head turns toward where the left stick has moved the
    // reticle, like the camera of the non-VR versions. 0 = off.
    float followAim = 0.0f;         // largest turn in degrees
    float followSpeed = 1.0f;       // how fast the aim estimate moves, 1 = matches the reticle
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
// convergenceKey: "Convergence", or "AreaXConvergence" inside Area X.
bool SaveStereoSettings(const char* dllDir, float separation, float convergence, float crosstalk,
                        const char* convergenceKey = "Convergence");
