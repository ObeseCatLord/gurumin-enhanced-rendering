# Gurumin Enhanced Rendering

A rendering and camera mod for **Gurumin: A Monstrous Adventure**, Steam version
1.4 (build 603361, app 322290). Supports Windows and Linux through Proton.

[Download](https://github.com/ObeseCatLord/gurumin-enhanced-rendering/releases/latest)
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

## Install — copy into the game folder

Close Gurumin. Extract the ZIP, then copy the **contents** of its single
`Gurumin-Enhanced-Rendering-1.0.1` folder into the game folder beside `game.exe`.
The top-level `d3d9.dll` is the mod. **No installer or executable patch is required.**
Start the game normally through Steam. The mod uses your desktop resolution on
first launch; select another resolution in the game's Graphics / Audio settings.
Preferences are saved by the launcher, so the ZIP does not overwrite an existing INI.

On Linux/Steam Deck, set this per-game Steam launch option once:

```text
WINEDLLOVERRIDES="d3d9=n,b" %command%
```

The optional Linux installer can do this automatically. Close Steam while it
updates that setting. If you already use other launch options, the installer
preserves supported wrappers, arguments and other DLL overrides.

### Optional installers

The **Optional installers** subfolder contains the Windows `.exe` and Linux `.sh`.
Use them if you prefer automatic Steam-library detection or drag/drop installation.
They only install/configure the mod; they do not launch the game.

- Windows: run `GuruminEnhancedRendering.exe`, or drop the game folder/executable on it.
  Its Python runtime is included; Windows 10/11 x64 is the target installer platform.
- Linux: run `GuruminEnhancedRendering.sh` in a terminal (Python 3.9+, no pip packages),
  or paste/drop the game folder/executable into its path prompt.
  It detects native/Flatpak Steam libraries and sets the required launch option.

The installers can be run before or after copying this release's DLL. They
preserve preferences and create verified backups. An unknown existing D3D9
wrapper is refused. `--steam-root PATH`, `--steam-user ID`, `--game PATH` and
`--list` are available for advanced selection. Linux `--no-terminal` avoids
opening an additional desktop terminal.

### Configure the game

Start Gurumin normally through Steam. Open **Graphics / Audio** in the game's
settings window and choose your resolution, cap and camera settings. **More
settings** provides shadow resolution, AA and texture filtering. Choose **Start
Game** or **Close** to save; Cancel/Escape discard changes. Restart to apply.

For DualSense or Steam Controller through Steam Input, configure the right stick
or pad as a **right joystick** rather than mouse/keyboard input. Enable Free
camera in the settings to use it. Movement and button bindings remain native.

The optional installer uses your detected primary desktop resolution, or
1920×1080 if detection is unavailable. An explicit `--width W --height H` overrides
that choice. Updates keep the prior installed resolution and edited INI.

## Uninstall and troubleshooting

For a manual copy installation, close the game and remove `d3d9.dll` and
`GuruminModern-FXAA-LICENSE.txt`. Keep or remove `GuruminModern.ini` as preferred.
On Linux, remove the `d3d9=n,b` override from Steam launch options while retaining
any unrelated overrides and arguments.

If you used an optional installer, close the game (and Steam on Linux), then run
it with `--action restore`. It restores verified originals and its Steam options,
retains later user edits, and leaves backups under `GuruminModern-backup`.
`--action status` reports installed files and hashes. Saves are never changed.

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
