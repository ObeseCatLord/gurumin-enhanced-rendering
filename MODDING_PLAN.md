# Gurumin Enhanced Rendering original plan

Install: Steam app 322290, build 603361, 32-bit native Falcom engine with Direct3D 9.
Reference SHA256: 4a27c727160d2565f02488883e86a34ac6e0f650aee528f0a1e24017eedc5e9e.
Target display: 3440x1440, primary DP-4, 174.91 Hz.

1. Analyze stock mode selection, camera projection, 2D draw path and frame loop.
2. Implement a narrowly scoped native runtime patch, retaining the game's renderer,
   input, simulation, sound and save system. Replace no engine subsystem.
3. Expand horizontal field of view at constant vertical framing; scale and center
   UI uniformly, preserve full-screen fades/backgrounds and mouse hit testing.
4. Separate presentation from fixed-rate gameplay only where binary evidence proves
   coupling. Counter readings alone do not establish smooth animation or correct speed.
5. Verify running menus, gameplay, movement, camera, animation, dialogue and transitions
   against stock, at 4:3, 16:9 and 3440x1440, and at baseline and 175 FPS.
6. Deliver source, binary, documented settings, install and rollback utilities, and
   evidence with explicit remaining limitations.

## Architecture comparison
A minimal proxy DLL plus checked game hooks preserves the working engine and avoids
new simulation state/policy. A renderer or engine rewrite would duplicate device,
resource, input and scene state without evidence of incompatibility; reject it.
A graphics wrapper alone can unlock a backbuffer but cannot prove simulation timing
or correct HUD/camera behavior. The smallest proof is stock title/gameplay at native
3440x1440 with proportional HUD and widened view, followed by timing comparisons.

## Lab and rollback
Research and decompiled output stay outside this source directory, in a private research directory.
Pre-session files, profile and saves are backed up in private research evidence (excluded from the release)
Original executable backups in the install have been hash verified. Existing patched
executables are preserved in the session backup. Test input is restricted to the game.

Additional acceptance requirement: internal scene/color/depth and post-processing
render targets must be inspected at runtime. A high-resolution backbuffer wrapping
low-resolution scene rendering is not sufficient. Confirm native scene detail.
