# Launcher, cap and dual-analog plan

Steam 1.4 x86, stock hash checked. User confirmed judder fixed before this work.
Original renderer, pose interpolation, HUD, rhythm, native camera collision and simulation remain the reference.

Senior review: Astra (`gpt-6-astra`, verified `xhigh`), 2026-10-07.

| Recommendation | Disposition |
|---|---|
| Keep user pitch separate from native collision correction | Adopted. Add offset only at five verified native pitch callsites; never rebase native correction by writing user pitch into it. |
| Tick-based angular speed, native collision/follow retained | Adopted. Integrate once per simulation tick and rebase after suspension. Native manual flag only on accepted stick movement. |
| Reuse selected Steam Input controller, one fresh sample owner | Adopted. Capture through existing XInput pointer at native input polling. Reject failed/stale samples, focus loss and identity changes; only consume right-stick axes during eligible gameplay. |
| Optional camera failure must not disable existing rendering hooks | Adopted. Separate three-hook camera group, local rollback; one MinHook initialization. |
| Save only after native launcher acceptance | Adopted. Draft settings, temporary valid legacy resolution index, native validation retained, rejected/Cancel changes do not persist. Native MFC accepted-result callsite confirms the transaction. |
| Single authoritative requested resolution and coherent fallback | Adopted. Persist dimensions for all launcher choices. Missing new keys preserve installed/native selection. Recompute native aspect and RT edge before resources. Caps/fullscreen validation, requested/effective values logged separately. |
| Pace after successful Present | Adopted. Absolute QPC deadline, immediate presentation for numeric/uncapped; zero follows native VSync. Reset on discontinuities, no simulation changes. |
| Replace launcher/renderer/input subsystem | Rejected. One original-dialog adapter and native hooks suffice; no duplicated subsystem. |

Execution order:
1. Finish native launcher acceptance and pitch ABI/callsite checks.
2. Add pure settings/rate/deadzone logic, original-dialog adapter and presentation pacing.
3. Add optional native input/orbit/pitch hook group.
4. Build and run meaningful portable regression tests, compile actual x86 hook fixtures.
5. Install transactionally with original backup retained, validate launcher and gameplay when desktop access permits.

Required acceptance: save/cancel/rejected validation/reopen, native and ultrawide modes with actual internal RT sizing, fullscreen fallback, frame-cap cadence at unchanged native game speed, orbit four directions while moving/near walls, release/disconnect/focus/menu/script takeover.

Desktop access restored. Real-game acceptance is in progress; record observed results below.

## Final implementation follow-up

- Accepted transaction is proven at the native MFC EndDialog import caller, after OnOK's UpdateData validation. Both native acceptance handlers use IDOK; Cancel uses IDCANCEL. Native helper6EFC30 is a no-op and is not a save oracle.
- Optional camera implementation review confirmed all RVAs, pitch ABI and five direct callsites; its HUD/reset angle-loss finding was corrected. HUD availability gates input, while native camera/script state owns pitch lifetime.
- Slow-render pacing test caught an unnecessary extra interval after an over-budget frame. The pacer now returns immediately for that frame and rebases without a catch-up burst.
- Executable IAT bootstrap import identity/callsite and native input's plain-ret ABI were independently confirmed from stock disassembly.
- Compilation, portable/installer tests and the Windows fixture pass. Live save succeeded, actual scene viewport is 3440x1440 with high-resolution color/depth allocations, measured cap is ~175 FPS at unchanged ~30 Hz simulation. Steam Input yaw/pitch samples are observed; user feel and camera collision checks remain.

## Additional display follow-up

User requested full-screen scrolling title pattern, full-screen area-transition
blackout and correction for skybox judder. Trace exact semantic callers, extend
background/fade coverage only, and reuse the same filtered camera for the skybox
without interpolating gameplay state or distorting foreground artwork/HUD.

The initial save error was a Windows API contract mistake: the profile-cache
flush returns zero even on success. Individual key writes remain checked; flush
the physical temporary file with FlushFileBuffers before atomic replacement.
The native fixture reproduces and verifies the corrected accepted save path.

## Camera follow-up: second Astra review and disposition

Reviewer gpt-6-astra, verified xhigh, 2026-10-07. First attempt at capacity;
retry succeeded. Live outdoor diagnostic proved script=0/mode=0/special=0,
manualA30=0/block=0/activecontrol=1. User confirmed indoor axes already work.

| Recommendation | Disposition |
|--|--|
| Extend only confirmed mode0 fixed gameplay, scoped A30 override | Adopted. Restore authored flag after native camera; mode5 exceptions excluded. |
| Respect native A34 eye/target holds | Adopted. Native holds own the camera and suppress adapter mutation. |
| Contract final returned camera after native smoothing | Adopted candidate. Reuse native wildcard segment cast, preserve target/return, 30-unit margin; live floor coverage pending. No forced minimum zoom through geometry. |
| Geometric ±80° pitch bounds and anti-windup | Adopted. Main calls constrain stored user offset; all five calls constrain actual elevation. Native correction storage unchanged. |
| Start without separate release state | Adopted. Shared engine smoothing and existing renderer remain; add no parallel camera history. |
| Scene rebasing | Adopted. Native map identity resets user offset; ordinary focus/menu/device reset retains pitch. |

Independent X/Y inversion is integrated into the original launcher. These new
camera changes and full-screen title/iris/solid fade candidates are staged, not
installed or visually claimed. Next game test is delayed until the requested
sky/shadow/AA/filtering features have their own verified Astra plan and are ready.
