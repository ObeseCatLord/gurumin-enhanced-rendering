# Changelog

## 1.1.0 — 2026-10-14

- Optional 6DoF OpenXR VR mode (default off; launcher toggle or `-vr`):
  third-person camera rig that follows Parin, Life size or Diorama scale,
  smooth right-stick orbit and rig height, head-relative left-stick movement,
  gamepad and VR controllers (Touch, Index, Vive, WMR, simple), recenter by
  clicking both sticks.
- Single-pass stereo: one scene traversal, each eye-dependent draw issued per
  eye region of a side-by-side target; eye-aware culling and all map zones drawn.
- HUD on a soft head-following panel; title, menus and loading on a virtual screen.
- Native screen fades and the iris transition cover the whole view in both eyes;
  cinematic letterbox bars are omitted in VR. The rig is never moved by walls.
- x64 OpenXR host (`GuruminVR/`) started by the game on Windows and Proton;
  zero-copy DXVK shared images under Proton, CPU copy on native Windows; host
  restarts automatically if the runtime starts later or stops.
- Optional installer manages the `GuruminVR` files and adopts a copied release.
- The game keeps running in VR without window focus, and skips the desktop
  window by default (`VRDesktopMirror=1` restores it) so a covered window can
  no longer throttle the headset. Back buffer MSAA is unused in VR and disabled.
- Culling and map cell selection follow the headset: the native camera used
  for visibility is the head with a frustum covering both eyes, so scenery such
  as the monster village is complete in every direction.
- HUD panel sits slightly below the gaze (`VRHudDrop`, 7°); diorama rig closer.
- Desktop: water reflections follow the interpolated camera instead of stepping
  at the 30 Hz camera tick at high frame rates.

## 1.0.4 — 2026-10-08

- Replace Interiors only with Out of town only, enabled by default, excluding
  just the main town exterior while allowing freecam in rooms and dungeons.
- Align fixed-camera freecam movement with the visible camera direction while
  retaining authored anchor following and native movement timing.
- Document the successfully tested GE-Proton10-34 Steam setup and combined
  renderer/outfit DLL overrides. The optional installer still configures launch
  options; selecting the compatibility tool remains a Steam setting.

## 1.0.3 — 2026-10-08

- Extend the native Filtering dropdown through 16× anisotropy, retaining its
  original configuration format and respecting GPU limits and nearest sampling.

## 1.0.2 — 2026-10-07

- Fix Linux installation refusing a closed Steam client because overlay or
  logging helpers survived shutdown. Verified installation on the actual system,
  correct per-game launch options, repeat-install idempotence and Astra recheck.

## 1.0.1 — 2026-10-07

- Copy the single release folder's contents directly into the game directory.
- Installers are optional; Linux launch settings remain documented and automatic
  through the optional installer.
- Fresh DLL-only installs choose the desktop resolution; copied updates and
  subsequent optional installs preserve preferences and verified recovery.

## 1.0 — 2026-10-07

Initial public release, **Gurumin Enhanced Rendering**.

- True higher internal resolution and Hor+ ultrawide output.
- Proportional HUD anchors, complete menu canvas and widescreen overlays.
- Render interpolation with unchanged native gameplay timing and selectable cap.
- Optional Steam Input dual-stick camera, X/Y inversion and interiors restriction.
- Configurable shadow resolution, pre-HUD FXAA and independent texture filtering.
- Reward-preview ABI, rigid-equipment shadows, camera-matrix consistency,
  authored camera turns and outdoor Manual camera fixes.
- One Windows/Linux ZIP: copy its folder contents into the game directory; optional installers provide Steam detection and drag/drop paths.
- Linux per-game Steam launch-setting setup and reversible, verified installation.
- Free camera and Interiors only default off; Invert X defaults on.
