# Gurumin Enhanced Rendering

A rendering and camera mod for **Gurumin: A Monstrous Adventure**, Steam version
1.4

[Download](https://github.com/ObeseCatLord/gurumin-enhanced-rendering/releases/latest)

[PSP Outfits](https://github.com/ObeseCatLord/gurumin-enhanced-rendering/blob/main/Gurumin%20PSP%20Outfits.zip)

I recommend mods from [Tenome](https://github.com/Tenome/Gurumin-Modding) which include script fixes, Japanese voice acting, and other enhancements

## Features

- Actual higher internal rendering resolution, including 3440×1440 and other
  ultrawide modes, with Hor+ framing at a constant vertical field of view.
- Proportional HUD
- Smooth render interpolation for characters, rigid attachments, camera, rhythm
  track, effects and shadows while gameplay keeps its original 30 Hz simulation.
- High framerate support, plus native VSync
- Right stick free camera as an option. The game isn't made with it in mind, can look weird in town
- Higher Resolution Shadows
- FXAA

## Install — copy into the game folder

You can use the optional installer to automatically find and install the mod for you, if you are too lazy to drag & drop it
On Linux/Steam Deck, set this per-game Steam launch option once:

```text
WINEDLLOVERRIDES="d3d9=n,b" %command%
```

The optional Linux installer can do this automatically. Close Steam while it
updates that setting. If you already use other launch options, the installer
preserves supported wrappers, arguments and other DLL overrides.

### Configure the game

Start Gurumin normally through Steam. Open **Graphics / Audio** in the game's
settings window and choose your resolution, cap and camera settings. **More
settings** provides shadow resolution, AA and texture filtering. Choose **Start
Game** or **Close** to save; Cancel/Escape discard changes. Restart to apply.


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

Created by **ObeseCatLord**,
Original mod code is MIT licensed. MinHook and NVIDIA FXAA retain their respective
licenses; the bundled Windows Python runtime retains the PSF license. See
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). No generated art/audio is used.
Gurumin and its game assets belong to their respective owners.
