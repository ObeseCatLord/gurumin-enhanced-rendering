# Graphics and camera follow-up plan

Supported reference: Steam Gurumin 1.4, build 603361, x86 Direct3D 9. Keep the
existing resolution, Hor+, HUD anchoring, render interpolation, launcher
acceptance, and native simulation unchanged. Do not launch another game test
until this whole candidate is implemented and its isolated checks pass.

## Investigation and Astra disposition

The verified brief is private research evidence (excluded from the release).
Astra (`gpt-6-astra`, verified `xhigh`) reviewed the actual native evidence and
source. The main agent checked the alternate projection construction, shadow
binder bookkeeping, intro submission order, and iris blend/address enum.

| Recommendation | Disposition and implementation |
| --- | --- |
| Use the native scene blit before UI for spatial AA | Adopt. Exact return RVA 3A7E21, scene source identity, primary destination, ordinary null pixel shader, supported gameplay HUD callback. Two strengths of mod-owned Edge AA; preserve native effect branches. No scratch target or new compositor. |
| Clamp every AA sample to valid scene content | Adopt. Derive texel-center bounds from native quad UVs and texture dimensions; use bilinear sampling temporarily, restore shader/constants/samplers immediately. |
| Keep HUD outside AA | Adopt. HUD callback follows the selected blit. Earlier world-space atlas markers are part of the scene; text bubbles in the gameplay HUD callback remain outside AA. |
| Preserve the actual alternate sky projection | Adopt. Factor alternate VP with projection RVA 975D18, validate recovered view against native main view, reuse the existing main-view interpolation history. Reject guessed projection reconstruction and name-only substitution. Visual sky continuity remains an acceptance check. |
| Enlarge shadow allocation, viewport, canvas and depth together | Adopt. Restrict to ten native shadow resources, offer 256/512/1024/2048. Preserve native ownership and projected world coverage. Verify actual dimensions, common depth compatibility, named-cache lifecycle and uniform failure behavior. |
| Independent UI/world nearest sampling at primitive submission | Adopt. Classify asset sampler roles and scene scopes independently of interpolation. Projected marker/bubble artwork is UI. Exclude scene composition, masks, video, shadow textures and lookup samplers. Restore actual device states without changing native sampler cache. |
| Restrict solid fade coverage to proven callers | Adopt. Full native canvas plus transition return RVA 1DF06A; other rectangles retain original behavior. |
| Iris uses CLAMP, not BORDER | Adopt correction. Extend horizontal geometry with the original UV slope and centered canvas. Preserve phase-dependent center and circle dimensions. Verify opaque black edge coverage during combined test. |
| Cover intro wings after character submission | Adopt. Intro fade boundary return RVA 1F3866 occurs after video and character draws. Insert state-restored black wings there, before the native fade. Keep central video proportional. |
| Four-field local More Graphics draft | Adopt. Shadow resolution, Edge AA, UI filtering and world filtering in a small modal; Apply copies to parent draft, Cancel/Escape/X discard. Only existing native parent acceptance saves the INI. |

## Execution and acceptance

1. Implement the narrow render adapters and graphics fields/modal. Keep each
   optional hook failure isolated from working display/interpolation hooks.
2. Run portable math/settings checks, native ABI/state/acceptance fixtures,
   shader compilation and GPU checks where available, then installer checks.
3. Install transactionally and run one combined candidate. Leave the launcher
   open to verify all controls and save/reopen behavior before starting play.
4. Check title pattern, opening wings, solid/iris transitions, outdoor orbit
   with both inversions, low/high pitch and floor contraction, sky continuity,
   actual shadow resources/coverage, AA edge improvement and all four texture
   filtering combinations. Confirm native gameplay speed and existing HUD
   layout remain intact at 3440×1440 / 175 Hz.
5. Record verified results and any remaining visual limitation. Offline counters
   and fixtures alone are not proof of a user-visible graphics fix.

## Follow-up: camera entry, interiors restriction, visible AA

The combined ultrawide tests verified 3440x1440 scene resources, all ten 1024px
shadow targets, pre-HUD shader execution and native sky-camera factoring. The
user confirms shadows and general orbit controls look good; the initial outdoor
view and visible AA remain acceptance failures.

The focused brief is private research evidence (excluded from the release); Astra
model/effort were verified as `gpt-6-astra`/`xhigh`. Plan: preserve authored fixed
camera updates until fresh right-stick engagement, retain the existing scene
blit and replace the simple custom filter with licensed FXAA3.11, and add
`CameraInteriorsOnly` as an optional restriction under `FreeCamera`. Classify
interiors using native evidence; an authored/manual camera flag alone is not
sufficient evidence. Unknown areas use the native camera when restricted.

| Follow-up review recommendation | Disposition |
| --- | --- |
| Separate fixed-camera takeover latch from integration priming | Adopt. Native authored updates remain active until fresh nonzero input; first input primes without applying a delta. Native manual reconstruction may still adjust radius/elevation: do not claim exact continuity without live comparison. |
| Clear and scene-tag pending input at ownership resets | Adopt. Prevent stale cross-scene engagement; retain ownership/pitch through focus, menu and disconnect suspensions. |
| Interiors restriction gates ownership | Adopt. Default false, unknown/outdoor remain native when enabled; checkbox retains preference while master disabled. |
| Explicit nonlinear luma contract | Adopt. Query sampler sRGB decode and render-target write state; only support established nonlinear scene sampling. Synthetic luma at all FXAA reads, retain original center alpha. |
| Real diagonal/padding/alpha/HUD readback checks | Adopt. Expand isolated D3D9 fixture; supplement with native-resolution live A/B. |
| Compile distinct presets atomically; benchmark High | Adopt. Presets12/39, no new surfaces or additional samplers, retain existing INI numeric values and NVIDIA license. |

## Outdoor anchor follow and movement lines

User steering supersedes the fixed-camera takeover: authored A30 remains0 at
all times. Astra xhigh verified the sole native call's separate stack outputs,
copy direction at76F1D0 and fixed-path exit715EBD..715F0C. Main disassembly
spot-check confirmed native desired/actual/carry copies precede return. Apply
persistent user yaw/elevation only to independent final output buffers; native
tracking keeps advancing. Reset at map/ownership changes, not anchor-index
changes. Clamp geometric elevation and apply anti-windup only on vertical input.
Outdoor extra contraction requires fresh eligible lowering direction, including
repeated same-tick calls; it is not a persistent floor-clearance mode. Indoors
retain the existing floor behavior. Release defaults are FreeCamera=0, CameraInteriorsOnly=0 and InvertCameraX=1, following the later user request.

Native initialization explicitly chooses intervalONE when VSync is enabled
(settings-native.txt:698), but numeric frame caps overrode it with IMMEDIATE.
Numeric caps now preserve native VSync. Explicit Uncapped alone remains
IMMEDIATE. Whole-picture movement lines are suspected tearing; user clarification
is pending, so this is not yet proof that every reported line is fixed.

Actual-eye rendering consumers are under a focused review to ensure the output
adapter does not reintroduce camera-relative particle/billboard errors before
final testing. No new game run until all programming is complete.

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
