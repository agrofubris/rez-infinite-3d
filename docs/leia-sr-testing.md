# Leia SR test checklist (v0.3.0)

For anyone with a Leia SR panel (Acer SpatialLabs, Samsung Odyssey 3D, ...).
Each step takes a minute or two. Please send `REZ\rez3d.log` and the captures
with your report (the **Leia SR test report** issue form).

## 0. Setup

1. Install v0.3.0 (`install.ps1`), then in the launcher set **format** to
   **leia sr**, render scale 1.0.
2. In `REZ\rez3d.ini` set `DumpFrames=2`, `DumpAfterSec=40`, `DumpEverySec=5`.
   Start the game and be in Area 1 within 40 seconds; two captures are written
   to the `REZ` folder (`rez3d_frame_*.bmp` = what was shown, `rez3d_pair_*.bmp`
   = the side-by-side pair the weaver received).
3. Note your Windows display scaling (Settings > Display) and resolution.

## 1. Run with the defaults

- Does the panel switch to 3D and stay there?
- Do your eyes each see one clean image, or both images (doubled, the doubling
  growing with separation)?
- Hotkeys (keep the game focused): Ctrl+F3/F4 separation, Ctrl+F5/F6
  convergence. Both should change the depth and show a readout at the top.
  If nothing reacts, say so: hotkeys were broken in v0.2.0.

## 2. The log lines that matter

In `REZ\rez3d.log`, copy every line that starts with `Leia SR`. The important
one is:

    Leia SR geometry: monitor \\.\DISPLAY1 (0,0)-(3840,2160) mode 3840x2160 (desktop 3840x2160) dpi 240 | weaver window ... client (0,0)-(3840,2160) | 3D view (0,0)-(3840,2160) | swapchain 3840x2160

For a clean weave, the monitor rectangle, the display mode, the weaver
window's client area, the 3D view and the swapchain should all be the same
size, starting at the monitor's corner. When they aren't, the log adds a line
saying which one differs, for example:

- `the display runs 1920x1080, not the desktop's 3840x2160` - the game changed
  the display mode and the panel is scaling the image, which breaks the weave.
  Set the game's own resolution to the panel's native one and retry.
- `the 3D view does not cover the whole monitor` - the game window is smaller
  than the screen or on another monitor.
- `the weaver's window differs from the 3D view` - try step 3.
- `the swapchain is stretched to the window` - should no longer happen in
  v0.3.0; please report it.

Also useful: `Leia SR: weaver ready (lens hint on)` (or the reason it was
unavailable), `Leia SR: pair WxH, woven output WxH` and `Presenter: output ...
-> ...` if the output was resized to the window.

## 3. LeiaWindow=own

Set `LeiaWindow=own` in `REZ\rez3d.ini` (default `top`) and repeat step 1. This
hands the weaver the 3D view itself instead of the game's top-level window.
Does the panel still switch to 3D? Is the doubling better, worse or the same?

## 4. If it is still doubled

- Repeat with Windows display scaling at 100 % (the log's `dpi` should read 96)
  and say whether it changes anything.
- `REZ3D_DISABLE_LEIA_WEAVE=1` (environment variable) forces the plain
  side-by-side fallback; that confirms the 3D render itself is fine.
- A phone photo of the panel up close (a few centimetres) next to the capture
  helps tell a weave offset from tracking trouble.
