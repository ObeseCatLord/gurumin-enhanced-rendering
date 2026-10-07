# Gurumin Enhanced Rendering

A rendering and camera mod for **Gurumin: A Monstrous Adventure**, Steam version
1.4 (build 603361, app 322290). Supports Windows and Linux through Proton.

[Download v1.0](https://github.com/ObeseCatLord/gurumin-enhanced-rendering/releases/tag/v1.0)
— **one ZIP for both platforms**. No original game executable or asset is included.

## Features

- Actual higher internal rendering resolution, including 3440×1440 and other
  ultrawide modes, with Hor+ framing at a constant vertical field of view.
- Proportional HUD: HP/minimap on the left, equipment/menu on the right, rhythm
  track centered. Menus and cinematic overlays cover the widescreen canvas.
- Smooth render interpolation for characters, rigid attachments, camera, rhythm
  track, effects and shadows while gameplay keeps its original 30 Hz simulation.
- Frame-cap presets, up to and including 175 FPS, plus native VSync and uncapped.
- Optional Steam Input right-stick orbit, independent X/Y inversion, an interiors
  restriction, authored outdoor anchors and floor collision handling.
- 256/512/1024/2048 shadow resolution; native AA or FXAA 3.11 Low/High before the
  gameplay HUD; separate native/nearest filtering for world and UI textures.

**Free camera and Interiors only default off. Invert X defaults on.** Existing
preferences are preserved on update. Interpolation adds roughly one simulation
tick of visual latency; it does not speed up gameplay.

## Install

Close Gurumin and extract the **entire ZIP** to a writable folder.

### Windows

Run **GuruminEnhancedRendering.exe**. It detects Steam and its library folders,
then patches an installed copy. If there are several copies, select one.

If your installation isn't detected, drag its game folder, `game.exe`, or
`gurumin.exe` onto **GuruminEnhancedRendering.exe**. You can also paste that path
into the patch's prompt. The ZIP includes its own Python runtime; no separate
Python installation is needed. Windows 10/11 x64 is the target installer platform.

### Linux / Steam Deck

Requires Python 3.9 or newer (no pip packages). Close **Steam** as well as Gurumin
before installing so Steam cannot overwrite the updated launch settings.

Run **GuruminEnhancedRendering.sh** in a terminal, or choose your file manager's
**Run in Terminal** action. If necessary, mark it executable first:

```sh
chmod +x GuruminEnhancedRendering.sh
./GuruminEnhancedRendering.sh
```

The patch detects native and Flatpak Steam installations and external libraries.
If no copy is found, drag the game folder or executable into the patch's path
prompt. File-manager drag/drop onto the script also works where supported:

```sh
./GuruminEnhancedRendering.sh '/path/to/Gurumin A Monstrous Adventure'
```

The installer adds the per-game D3D9 loading setting to the selected Steam
account's launch options, while retaining supported existing options and other
DLL overrides. It saves the original options for uninstall. Custom launch syntax
that cannot be safely merged is rejected before game files are changed.

For an advanced manual setup, the required Steam launch option is:

```text
WINEDLLOVERRIDES="d3d9=n,b" %command%
```

Portable Steam roots can be specified with `--steam-root PATH` (repeatable).
Use `--steam-user ID` when the active Steam account cannot be determined.
`--list` only lists copies. `--game PATH` selects a particular installation.
The shell's `--no-terminal` flag runs directly without opening a desktop terminal.

### Configure the game

Start Gurumin normally through Steam. Open **Graphics / Audio** in the game's
settings window and choose your resolution, cap and camera settings. **More
settings** provides shadow resolution, AA and texture filtering. Choose **Start
Game** or **Close** to save; Cancel/Escape discard changes. Restart to apply.

For DualSense or Steam Controller through Steam Input, configure the right stick
or pad as a **right joystick** rather than mouse/keyboard input. Enable Free
camera in the settings to use it. Movement and button bindings remain native.

The first installation uses your detected primary desktop resolution, or
1920×1080 if detection is unavailable. An explicit `--width W --height H` overrides
that choice. Updates keep the prior installed resolution and edited INI.

## Uninstall and troubleshooting

Close the game (and Steam on Linux), then use the same patch:

```text
GuruminEnhancedRendering.exe --action restore
```

```sh
./GuruminEnhancedRendering.sh --action restore
```

Add `--game PATH` if needed. Restore uses verified originals under
`GuruminModern-backup`, restores installer-owned Steam options, and keeps later
user edits. Backups remain available; saves are never changed. `--action status`
reports the installed files and their hashes.

Only the supported Steam executable is accepted, verified by full SHA-256.
Unknown executable modifications or another `d3d9.dll` are refused. Resolve an
existing graphics-wrapper conflict first. Steam file verification may restore
executables while leaving the proxy DLL; use this patch's restore command before
repairing or reinstalling the game.

`GuruminModern.ini` and `GuruminModern.log` remain the runtime filenames for
compatibility with earlier development builds. Set `Diagnostics=1` to record
render targets, effective dimensions and interpolation measurements; it defaults
to 0. Reverting that setting turns detailed diagnostics off on the next launch.

## Compatibility and limitations

- This release targets Steam 1.4/build 603361, not every retail executable.
- High resolutions are subject to GPU texture limits and memory availability;
  unsupported output/resource requests fall back to desktop windowed output.
- FXAA is spatial AA. HUD text and menus are outside the gameplay filter;
  world-space markers already rendered into the scene are included. Unsupported
  native effect paths retain their native shader and sampling behavior.
- Interiors only currently recognizes Parin's room, the old man's room, the cake
  shop and general store. Other dungeon interiors are not reliably classified.
- The game remains a 30 Hz simulation; interpolation smooths rendering rather
  than converting its entire gameplay and animation system to a new tick rate.
- Runtime validation included 3440×1440 at approximately 175 FPS with 30 Hz gameplay,
  Steam Input orbit, equipment rewards, menus, shadows and the outdoor Manual.
  Recent cinematic/attachment corrections also have isolated x86 regression tests;
  report remaining scene-specific issues with a reproduction and screenshot.

## Build and tests

Build the x86 D3D9 proxy using MinGW-w64 GCC/G++:

```sh
./build.sh
python3 -m unittest discover -s tests -p '*_test.py'
python3 tools/package.py
```

The packager builds the x64 Windows launcher and downloads the official Python
embeddable runtime with a pinned SHA-256. It creates one Windows/Linux ZIP and
`SHA256SUMS` under `release/`. Existing precompiled, mod-owned FXAA shader bytecode
is checked in; `tools/compile_edge_aa.sh` rebuilds it in an explicit Wine prefix.

Stock-executable integration tests require your own game files:

```sh
GURUMIN_STOCK_DIR='/path/to/Gurumin A Monstrous Adventure' python3 tests/install_test.py
```

The fixture accepts stock executables or verified `.guruminfix-original` sidecars.
It creates temporary copies and never changes the live installation. The C++
portable tests are under `tests/`; `native_render_test.cpp` exercises the actual
proxy with a fake game image under Windows or Wine. See `SENIOR_REVIEW.md` for
review dispositions and `MODLOG.md` for reverse-engineering and validation notes.

## Credits and license

Created by **ObeseCatLord**, with Codex AI assistance and Astra design/ABI audits.
Original mod code is MIT licensed. MinHook and NVIDIA FXAA retain their respective
licenses; the bundled Windows Python runtime retains the PSF license. See
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). No generated art/audio is used.
Gurumin and its game assets belong to their respective owners.
