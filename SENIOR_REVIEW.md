# Senior render review disposition

Requested by the user. Reviewer: gpt-6-astra, explicit xhigh, effective settings
verified locally. Review was read-only against the source, native disassembly,
and actual final-vertex traces. Original reviewer and supplemental rhythm findings
are summarized here; no operational session logs are included.

| Recommendation | Disposition |
|---|---|
| Reject bad rhythm observations while continuing rendering prediction; compensate against actual native draw phase | Adopted. Timestamp advances through rejection; no zero-offset fallback after initialization. |
| Bound rhythm correction velocity and remove innovation-based hard resets | Adopted. Existing0.15s proportional correction capped at10% of nominal velocity. Initial experimental tuning. |
| Require distinct advancing observations before unexplained epoch reacquisition | Adopted. At least3 advancing cursor samples over0.15s; repeated same values cannot confirm a seek. |
| Use actual native/submitted palettes for rigid attachments | Adopted. Successful c11 submission publishes owned snapshots, including native fallback; no cached endpoint dependency. |
| Resolve attachment ownership deterministically within the native pass | Adapted. Exact native VP equality and unique structural parent-bone match; ambiguity declines transport. |
| Correct mode15 world/VP at the existing shader-draw boundary | Adopted. Native matrix construction retained; downstream filtering explicitly suppressed. Follow-up runtime attribution found additional layouts in modes30/31 and other scene modes. The same adapter now applies whenever the native scene VP validates, with temporary actual viewport dimensions for world drawing. |
| Give77fbe0 world-effect quads actual viewport dimensions without UI processing | Adopted. Scope only the proven helper; nested saved dimensions restored. Smoke attribution still requires visual verification. |
| Rewrite hierarchy/renderer or increase simulation frequency | Rejected as unnecessary. Existing renderer, pose histories, timing and capabilities remain. |
| Treat counts as visual acceptance | Rejected. Required cases are face/cane, gates, doors, wind trees and smoke under static and moving cameras. Vertex-frame animation remains a separate probe if residual wind/face stepping persists. |

Meaningful regression checks cover the actual attachment implementation: native
fallback, same-tick source changes, competing candidates, separate passes, expired
snapshots. Rhythm replay covers bad/missing/repeated observations, positive velocity,
rate limits, native-phase compensation, pause/resume, gap and song/seek epochs.

## Launcher and dual-analog follow-up, 2026-10-07

A separate requested Astra xhigh review covered the launcher, presentation cap and
native orbit-camera additions. See [SETTINGS_PLAN.md](SETTINGS_PLAN.md) for the
verified brief's resulting plan and recommendation disposition. It caught native
pitch-recovery rebasing risk, optional-hook rollback coupling, premature settings
saving, startup aspect dependencies, and cap-induced frame latency. A subsequent
implementation review caught HUD/reset pitch loss; that issue was corrected.
The working renderer and simulation scheduler were retained.

## Camera collision and fixed outdoor regions

Second Astra xhigh review caught native A34 camera holds omitted from the
original manual-input gate, and mode5's distinct authored-camera branch. The
adapter extends only the live-confirmed mode0 fixed gameplay case, with scoped
A30 override. It constrains final returned eye after native smoothing rather
than assuming a broader collision mask before smoothing is sufficient.
Geometric pitch limits apply at all five calls, with persistent anti-windup at
the main sites. See SETTINGS_PLAN.md for dispositions and pending live checks.

## Graphics follow-up — Astra xhigh, 2026-10-07

The verified brief and native evidence were reviewed by gpt-6-astra at verified
xhigh effort. See GRAPHICS_PLAN.md for the disposition table and acceptance
checks. The review changed the implementation: select the late pre-HUD scene
blit rather than an earlier compositor; use the actual alternate projection
rather than infer one; require coherent shadow canvas/depth changes; classify
marker/bubble artwork as UI; correct iris CLAMP semantics; cover intro wings
after character submission; restrict solid fades to a proven caller.

Main integration also corrected the shadow worker's exact-size depth check to
accept an adequate larger common depth surface, used direct return-address
capture inside the allocation hook, and added named-resource descriptor
revalidation after Reset. No new depth pool or compositor was introduced.

The source compiles; portable shadow/settings tests, eight transactional
installer tests, and the isolated native camera/render/settings fixture pass.
GPU AA tests and the combined game acceptance run remain pending.

## 2026-10-07 authored-camera handoff and FXAA

Astra `gpt-6-astra` at verified `xhigh` reviewed the focused brief and actual
native camera/shader artifacts. Main spot-checked native manual reconstruction
(`1050.0` camera vector and pitch/smoothing), the synthetic-luma center-alpha
read, and the shader compile configuration. Important changes from the draft:
scene-tag and invalidate pending input, separate persistent takeover ownership
from tick integration, require an explicit nonlinear sampling contract, and
add actual HUD pixel comparison rather than equating restored states with visual
HUD isolation. The camera fix preserves the authored path until stick input;
exact post-engagement radius/elevation continuity still requires native evidence.
Interiors-only defaults off and gates ownership, with unknown areas remaining
native. An indoor classifier must not be inferred from camera mode. Dispositions
and the incremental implementation boundary are recorded in GRAPHICS_PLAN.md.

## Final integration correction: authored carry and modal cameras

The initial output-only orbit proposal is superseded. Astra xhigh found that
5A9925 builds the view and 5A9A22/5A9A30 subsequently copy the original stack
vectors back to native carry state. Main disassembly verified both later copies.
The view hook at return RVA1A992A substitutes by-value vectors only. Actual eye
and target are published only during native world render1D3360 when raw vectors,
scene, and built view still match; conditional restoration preserves native
replacements. No new camera history or independent compositor was added.

| Review finding | Disposition |
| --- | --- |
| Output-only offsets feed back through the caller’s later carry copies | Adopt correction: keep native outputs/carry untouched, substitute by-value view arguments. |
| Billboard/shadow readers need the orbit eye | Adapt: publish during the verified world-render boundary only, with identity guards and conditional restoration. |
| Manual can reuse gameplay interpolation at a paused simulation tick | Adopt modal isolation using native tutebook allocation plus active flag; invalidate main camera history and suppress related VP substitutions. |
| Popup anchors are native tick positions and layout loses fractional pixels | Adapt existing world pose history at getter return1CD589; fractional panel/text group offsets after native projection/layout. Edge clamps and fixed menu bubbles remain exact. |

Native RE confirms tutebook allocated at63D5D0 (VA920468), active flag920880,
opening state6, page states1/2, closing5;5B8590 clears both allocation and flag.
Bubble getter773EF0 is thiscall with one stack argument; panel5E27F0 is
thiscall with six DWORD arguments and ret24. Main disassembly and fixtures
verify their ABI. Copies of MainVP at15E99D0 are byte-identical before HUD
projection, so the existing camera replacement applies to their contents.

Release defaults FreeCamera/InvertCameraX/CameraInteriorsOnly are0/1/0; explicit existing
preferences remain respected. Numeric caps preserve native VSync; only explicit
uncapped requests IMMEDIATE. No game was launched during this programming batch.

## First-goggles crash and broader ABI audit

The verified crash is a calling-convention error in projectedQuad37FBE0, not an
allocation failure. Native77FBE0 immediately calls textureBind7A58B0 with its
incoming ECX receiver. The hook had declared only six stdcall stack arguments;
viewport updates left ECX=1440 and7A58C1 dereferenced1440+1C. Corrected to
thiscall original and fastcall detour with spareEDX, preserving all six stack
DWORDs. The x86 receiver/argument/restoration fixture passes. Fault tracing is
local and diagnostic-only, continues exception search, and allowed exact
attribution rather than guessing from the final model log.

At the user's request the follow-on audit uses verified gpt-6-astra/xhigh.
The main-thread Ghidra native instruction ledger includes entry register use,
function extents and all return instructions for each native hook, plus shadow
allocator/binder. The inline verified brief is retained locally at
private research evidence (excluded from the release) No further game launch occurs
until review corrections and offline checks are integrated.

### Astra native-hook audit dispositions

Reviewer effective settings were independently verified by main as
gpt-6-astra/xhigh. Astra's inability to inspect its own session settings does
not change that verification. Reviewer additionally located and hash-verified
the stock PE and inspected native instructions/callers.

| Finding | Disposition / verification |
| --- | --- |
| Projected quad missing incoming ECX receiver | Adopted thiscall/fastcall correction. Native77FBE0 calls7A58B0 with incoming ECX; sixDWORDs/ret18h. Independent x86 tests now cover both modes, vertex-data/opaque high-bit arguments, nested scopes and zero viewport. |
| Shadow binder byte-return ABI exposed to DWORD consumers | Adopted unsigned native/hook result. Main confirmed7A4D6B..70 forms full EAX 0/1 and768F7A tests full EAX. Independent DWORD caller checks success/failure, missing target, passthrough and full high-bit register values; portable tests cover depth/viewport/fallback behavior. |
| Other receiver/stack cleanup contracts | Compatible after complete source/binary audit: models/shader6DWORDs; shadow receiver14; particles3; sprite13; skin4stdcall; cursor0/mouse7; popup anchor1/panel6; static HUD/fade/layout cdecl; camera input/update/pitch/look-at/world match. COM/Win32 use standard WINAPI. |
| Malformed parent chain excluding child or exceeding64 nodes | Adopted all-cycle and nonterminated-limit rejection, retaining native rendering on exclusion. Added readable cyclic/long-chain tests. |
| Same-frame reparent/address reuse | Adapted focused fixtures to verify no old-parent pairing and fresh captured owners/palette replacing same-address data. No broad teardown system was added absent demonstrated incompatibility; snapshot owners remove delayed raw-controller dereference. |
| Transient material/controller arrays | Adopted unreadable owner-table/primary/secondary cases, including valid primary with invalid secondary. |

Native x86 long-call bridges additionally exercise13DWORD sprite and14DWORD
shadow-receiver forwarding, ECX, fullEAX including zeroAL, and balanced returns.
Shadow binder allocation policy/working interpolation were retained.

## Accessory shadow / cinematic coverage follow-up

Astra xhigh was selected and its effective settings verified. The review approved
reusing the existing native model hook rather than introducing another renderer
or target-tracking layer. The reward crash fix remains in place.

| Recommendation | Disposition |
|---|---|
| Scope caster correction to root caller RVA369853 and descendants, mode16 and recognized square viewport; exclude Manual | Adopted. Rigid equipment uses the same actual-palette attachment transport as visible meshes; native light VP is copied unchanged. |
| Preserve pass-local palette provenance and differing-payload rejection | Adopted. No name whitelist or independent shadow pose history. Both submission orders, recursive ABI forwarding, stale/unrelated passes and 256–2048 targets covered. |
| Preserve identical stationary transforms and current endpoints exactly | Adopted at the existing packed-pose boundary; avoids quaternion roundoff in coplanar floor geometry. |
| Share a canonical computed VP between scene and receivers | Adapted. Materialize the existing sparse projection formula once, then transpose/copy its bytes. The review correctly warned that generic multiplication could include previously ignored projection terms. |
| Attribute shadow stripes only after visual testing | Adopted. x86 arithmetic discrepancy reproduced, but regression counters alone cannot prove visible stripes are gone. No global depth bias introduced. |

Native cinematic bar sources are separately verified in upper/lower HUD and
actor-script rendering. Only those sources with exact full-canvas horizontal
bounds and native bar geometry are expanded; Y, alpha, color and subtitles remain
native. Actor-script fullscreen fades likewise cover the output width.

### Authored-camera turn addendum

Astra approved a narrow correction inside smoothCamera after verifying the
coordinate-space error: a fixed eye at(3000,4000) turning10degrees changes view
translation by871.557units, exceeding the500unit teleport guard without moving.
The ruins log also records camera interpolation rejection, though that alone
cannot attribute every perceived hitch.

| Recommendation | Disposition |
|---|---|
| Interpolate camera-to-world pose using the existing camera history, then invert back | Adopted. Existing native camera, alternate-view validation, projection, reflection, simulation and free-camera policy remain. |
| Raise/disable the teleport guard | Rejected. Physical eye displacement still uses the existing500unit limit. |
| Verify stationary-eye/origin-independent turns, reflections and real teleports | Adopted. Fixtures cover five interpolation phases, translated origin, both handedness signs,500/700unit eye teleports and invalid inputs. |
| Require live authored-camera turning in ruins for acceptance | Adopted. Offline pose/counter checks do not alone prove the user-visible issue is resolved. |

## Release installer audit — Astra xhigh

The release preserves the checked binary patcher and uses one shared Python
installer behind the Windows and Linux entry points. Linux configuration is a
narrow adapter for the selected Steam profile's per-game LaunchOptions.
The installers do not launch the game.

| Finding | Disposition / verification |
|---|---|
| A reinstall after user-edited launch options could restore an obsolete baseline | Adopted. Capture the current user value when it differs from the last owned value; preserve the previous baseline only while ownership still matches. Regression includes another DLL override, wrapper and arguments. |
| Cancellation could leave partially patched executables without ownership metadata | Adopted. Persist the existing manifest before game replacements, catch cancellation for rollback, and retain exact previous/next owned hashes with verified backups when rollback fails. Fresh installs and updates are interrupted after every replacement and both manifest commits; a separate subprocess hard exit verifies recovery without exception handling. |
| Steam config could change after preparation or ownership-record saving | Adopted. Recheck the captured bytes immediately before replacement and retain recovery metadata on conflict. Tests inject unrelated changes during apply and restore. This narrows the race; it is not an atomic operating-system compare-and-swap. Steam must be closed. |

The Python regression suite passes 35 tests with owned stock fixtures. Release
packaging excludes game executables, assets, private traces and operational logs.

Astra's focused recheck found no remaining release blockers in this scope. Its
non-blocking cancellation-message correction was also adopted: a cancellation
after the core install has committed now asks the user to inspect status rather
than claiming every file change was rolled back. Packaged Linux and Windows
(Wine) end-to-end detection/install/update/restore checks passed, including
Unicode paths. Four portable C++ regression fixtures also pass.

## Direct-copy release layout

Astra xhigh approved retaining the existing runtime and installer, with three
corrections adopted after its focused recheck:

| Finding | Disposition |
|---|---|
| Adopted manual preferences reset on a later reinstall | Preserve every existing owned INI, not only bytes differing from its recorded hash. Adoption/reinstall/restore regression passes. |
| Copying a new release over an older installer manifest blocks restoration | Shared validation additionally accepts only this bundle's byte-exact DLL and license notice. Original backups remain verified; unknown files still refuse. Both reinstall and direct restore cases pass. |
| Desktop fallback replaces the optional installer's explicit resolution | Fresh INI creation carries the selected width/height. Existing preferences remain untouched. |

The final Python suite passes 37 tests. Both relocated optional entry points pass
packaged end-to-end checks. Actual Steam launch with unmodified stock executables
and a copied DLL starts at3440x1440 and allocates a3440x3440 internal target.
Astra's focused recheck found no remaining blockers in these changed boundaries.

### Real Linux installer test

The packaged Linux entry point was run against the actual native Steam client
and external Gurumin library. It saved the selected account's app322290 options
as `WINEDLLOVERRIDES="d3d9=n,b" %command%` and recorded the previously absent
option for restoration. Repeating installation leaves the entire localconfig
byte-identical. The game is still started through Steam, not by the installer.

This test exposed a false-positive Steam-running check: surviving gameoverlayui
and srt-logger processes were treated as the client. The guard now recognizes the
Steam client executable beneath the selected root and ignores helpers; regression
coverage retains rejection of an actual client and checks surviving helper names.

Astra xhigh approved the client-process correction. Its additional tests were
adopted: an executable actually named steam outside the chosen root must not
block installation, and a symlinked Steam root must still identify its client.
The actual native client launch-option write and repeat-install checks passed.

## Town camera policy — Astra xhigh, 2026-10-08

The requested design and implementation reviews used independently verified
gpt-6-astra/xhigh. The native movement caller, scalar-output ABI and existing
authored-camera carry path were checked before implementation.

| Recommendation | Disposition |
|---|---|
| Exclude the town by exact scene and asset identity | Adopted. Only the main town exterior is excluded; rooms and dungeons retain freecam eligibility. |
| Keep collision interior classification separate from camera ownership | Adopted. The existing floor/collision policy remains. |
| Adapt native movement outputs rather than replace the input or camera system | Adopted. Original helper runs once; only its proven gameplay caller can use the owned horizontal camera basis. |
| Preserve input magnitude, native return value and carry behavior | Adopted. Portable and x86 regression fixtures cover turns, diagonals, vertical pitch, zero input and scalar boundaries. |
| Keep ownership current without requiring a new right-stick event | Adopted. Held movement can use the engaged orbit while scene/view/tick guards still validate. |
| Treat hook installation as runtime evidence, not controller acceptance | Adopted. Steam startup installs all six hooks; new town joystick alignment still needs playtesting. |

## Release automation boundary — Astra xhigh, 2026-10-08

A fallback workflow was reviewed while direct GitHub uploads were unavailable.
Reviewer gpt-6-astra/xhigh was independently verified. After network access was
restored, that temporary adapter was removed; publication uses the existing
packager and direct GitHub release upload. The source-tag and draft checks below
also apply to that publication sequence.

| Finding | Disposition |
|---|---|
| An existing tag can differ from a release's target_commitish | Adopted. Resolve lightweight/annotated tags to their commit, require the exact workflow SHA, and never move existing tags. |
| Published-tag lookup is not a reliable draft lookup | Adopted. Identify a single release ID from the authenticated collection and validate that draft by ID. |
| A newer run must not claim a tag owned by an older untagged draft | Adopted. Validate draft ownership before creating any missing tag. |
| A malformed version can escape the packager staging path | Adopted. Validate numeric major.minor[.patch] before constructing paths or deleting generated staging. |
| Keep partial uploads unpublished and preserve existing releases | Adopted. Upload ZIP and checksum to an owned draft, then mark it public/latest after successful upload and source identity recheck. |

Local syntax, build, portable tests, all37 Python tests and ZIP integrity checks
passed. Packaging lint passed after excluding only the installed mod's own
byte-identical NVIDIA license from the game comparison reference. That notice
is retained in the release as required. Native x86 fixture compilation passed;
execution was not performed in the preparation session. The fallback workflow
was never deployed or executed.
