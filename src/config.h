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
    // Testing: which of the game's projection queries draws what. ProbeSeq logs the
    // order of the queries in some frames. ProbeKind (raw / matrix) and ProbeIndex
    // shift one query's frustum sideways by ProbeShift (tangent units); with
    // ProbeCycleSec, the next index every that many seconds from ProbeStartSec,
    // up to ProbeCount.
    int probeSeq = 0;
    std::string probeKind = "off";
    int probeIndex = -1;
    float probeShift = 0.3f;
    float probeCycleSec = 0.0f;
    float probeStartSec = 0.0f;
    int probeCount = 24;
    // Testing: hooks on the game's D3D11 draws. HookMode 1 logs a frame's draws per
    // shader pair every HookLogEvery frames; 2 also skips one pixel shader at a time,
    // HookHuntSec each, from HookHuntStartSec (to find the reticle's draw).
    int hookMode = 0;
    int hookLogEvery = 300;
    float hookHuntSec = 2.5f;
    float hookHuntStartSec = 40.0f;
    unsigned hookSkipPS = 0;        // HookSkipPS: hex hash of a pixel shader whose draws are always skipped
    unsigned hookDetailPS = 0;      // HookDetailPS: log every draw of this pixel shader in logged frames
    unsigned hookHuntDrawsOf = 0;   // HookHuntDrawsOf: hunt over this pixel shader's draws, one at a time
    // Rez's reticle: quads (4 vertices) drawn by this pixel shader with a texture of this size
    // and one of these DXGI formats: the octagon (BC3, 77) and, as tested, the centre cross
    // (B8G8R8A8, 87). The enemies' glows share the shader and a BC3 texture, but draw 10
    // vertices. HookSkipReticle=1 hides it (test).
    unsigned reticlePS = 0x56E63E45;
    int reticleTexW = 256, reticleTexH = 256, reticleVerts = 4;
    std::string reticleTexFormats = "77,87";
    int hookSkipReticle = 0;
    // Reticle depth: Rez draws its reticle about ReticleGameDistance meters away (measured:
    // 198.5 px of the 208 px parallax at infinity, convergence 20). ReticleDepth > 0 redraws it
    // at that many meters by shifting its draw sideways per eye. 0 = where the game puts it.
    float reticleDepth = 0.0f;
    float reticleGameDistance = 440.0f;
    float reticleShiftPx = 0.0f;    // testing: a fixed shift in eye-image pixels instead (positive = nearer)
    // ReticleAuto=1 puts the reticle at the depth of what is under it: the nearest scene depth
    // under its centre (its vertex shader is ReticleVS). Rez's depth is reversed: distance =
    // ReticleDepthScale / depth (0.36 from the avatar: depth 0.0186-0.0196 at about 19 m).
    // With nothing under it (sky), it goes to ReticleDepth, or where the game puts it if 0.
    // Whatever is nearer than ReticleIgnoreNearer meters (the avatar, the floor just below it,
    // the shots leaving it) is ignored; the median of the last frames rides over shots crossing it.
    int reticleAuto = 0;
    float reticleDepthScale = 0.36f;
    float reticleIgnoreNearer = 20.0f;
    std::string reticleDepthSource = "draws";  // draws: anything drawn there; buffer: the depth buffer only
    // Pixel shaders whose draws don't count for "draws": Rez's glow sprites (halos around
    // enemies and explosions, hazes, lock-on boxes) cover much more than the thing they belong to
    std::string reticleDepthIgnorePS = "56E63E45";
    int reticleProbeLogEvery = 0;  // testing: log what is drawn under the reticle every N frames
    std::string reticleDepthProbe;  // testing: "x,y,r" measures a box of the screen (pixels) instead
    unsigned reticleVS = 0x04688A8A;
    // ReticleScale: the reticle's size, x the game's (it draws it at a fixed pixel size, so it
    // looks smaller at higher resolutions and render scales). Grows around its centre.
    float reticleScale = 1.0f;
    // ReticleKeepInView=1: the controller bridge stops the aim stick at the edges of the
    // picture, so the reticle can't leave the view (Rez lets it go about 50 deg across and
    // 43 deg up/down, wider than most fields of view). The runtime measures where it is.
    int reticleKeepInView = 1;
    // ReticleFlat=1: draw the reticle facing the view, as it would look in the middle of the
    // picture, wherever it is: a flat picture of a wide view stretches whatever is off-centre
    // (VR headsets' lenses undo that). Same centre and depth; ReticleScale still applies.
    int reticleFlat = 1;
    unsigned hookHuntTexOf = 0;     // HookHuntTexOf: hunt over the textures this pixel shader samples, one at a time
    // Follow aim: the virtual head turns toward where the left stick has moved the
    // reticle, like the camera of the non-VR versions. 0 = off.
    float followAim = 0.0f;         // largest turn in degrees
    float followSpeed = 1.0f;       // how fast the aim estimate moves, 1 = matches the reticle
    // Right stick look: the right stick turns the view (degrees per second at full
    // tilt), to find the reticle or follow a boss off to the side. R3 recenters. 0 = off.
    float rightStickLook = 90.0f;
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
