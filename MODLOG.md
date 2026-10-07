# Gurumin Enhanced Rendering development journal

2026-10-06: Recon confirms Steam build 603361, x86 native/D3D9. Earlier patch only
changes 1920x1080 mode immediates and label to 3440x1440; this is insufficient proof
of camera/HUD/timing. Original game and gurumin executable backups are available.
Originals and research artifacts were retained in a private local backup.
KB search found no Gurumin notes. No known full display/timing fix found in research.
Chosen route: thin native proxy/hooks. Preserve engine; inspect timing before HFR.

RE findings: resolution initialization at VA60a57c/586 sets display dimensions
VA908b0c/10; VA60a590 independently sets square offscreen target edge (VA21e480c)
to1920. Prior patch omits this third immediate. Render setup VA79cf9b converts this
to float edge VA908b1c. Native scene viewport clamps width/height to this edge in
VA77dee0; explains why merely changing output resolution can upscale/crop scene.
Runtime baseline: RT1920x1920 with viewport1920x1080; secondary256x256 textures.
VA777770 builds perspective, callers60b3fb/60b4db/60b663 supply actual h/w,
then scale y by1.333 (VA779480), yielding correct h/w projection ratio. Culling
VA774a70 explicitly expands horizontal bounds using aspect relative to.75.
2D primitives use viewport width/640 and height/480, with selective corrections
through aspect globalVA908b20. Prototype normalizes 2D canvas and aspect together.

Initialization failure: exact message "s064 Initialize error." originates VA5f2cd0
when VA7343a0 returns0. Its crossfade texture requests go through VA71c410, which
rejects either dimension>2048 before D3D. Actual D3D RT3440 and depth allocations
succeeded. Backend VA77d950 has no such legacy guard and passes dimensions toD3D.
Patch width/height compare immediates VA71c41e/71c432 to max(2048,target edge).
Disabling prototype UI correction did not resolve failure, excluding UI as cause.

2026-10-06 HUD anchors: native HUD callbacks 5cf280/5d9b00 establish a draw
scope; full-size XYZRHW UI uses a uniform 640x480 canvas. HP/minimap/currencies
anchor left, inventory/equipment/menu right, rhythm track centered. The first
two solid HUD quads require actual shrunken scene bounds from21e8d58/5c; simple
translation leaves stale pixels. User confirmed anchors/menu coverage work.
World marker 5cdcd0 and speech5cd4f0 require projection-space correction and
cursor7aa2a0 callback requires inverse UI coordinates. Mouse7aa330 is thiscall.

Timing: Present600/~3.434s=174.7Hz, simulation delta103=30Hz. Bone upload
7a4360 stdcall4args uploads3*N vec4 constants c11, stable source palette+model
pointers. Runtime600frames/103uniqueposes proves30Hz visible skeletal cadence.
Render-only affine interpolation now outputs~592uniqueposes/600frames with
0 rejected player matrices, keeping tick clock30Hz. It adds one tick of visual
latency; camera still needs validation/smoothing. c8 object transform source
proven780150 thiscall6args, firstthree transpose vectors of model+24c.
Sprite78ae20 has an implicit texture receiver in ECX and ret52; Ghidra omitted
the receiver. Stdcall-only detour crashed7a58c1 readingnulltexture+1c; corrected
to fastcall wrapper/thiscall original.

Rhythm9e160c is floor(15*(musicSamples-songOffset)/(2646000/tempo)); stock
uses44.1 samples/ms. Audio7c55d0 cdecl(handle) is a read-only sample-position
reader via9094dc. Notes in5d0280..5d05b0 move2logicalpx per phase, opposite
directions. Center note uses15-entry integer bounce curve. Render-only sample
reader and fractional sprite offsets implemented; audio cursor quantization
still reported judder, testing continuous QPC clock softly synchronized to audio.

2026-10-06 23:20: Rigid attachment experiment follows model+74c ancestry,
controller+1108 bone-model table, and transports child world M by C^-1*F using
native/current and interpolated world skin matrices. Runtime recognizes player
face_char02bf/bm and hair (bone13), NPC attachments/head. User reports face and
old man's cane still twitch/displace. This experiment is not verified successful.
All pose families use shared frame time and tick stamp; gameplay remains30Hz.
Rhythm exact native sprite call returns: negativeX1d0362/1d0441/1d0543,
positiveX1d03cc/1d04a8/1d05ad, central bob1d0de9. Corrected old guessed direction
and missed central call. User still reports track twitch; final-vertex trace next.

Shadow receiver751ee0 is thiscall14DWORD ret56; slot2 scene VP feeds CPU culling
775320 and shader c0 plus dependent world*VP c12 in799880. Replacing slot2 before
culling under known3526bc root/recursive scope with visible interpolated VP
matches all receiver entries in runtime (e.g.115467/115467). User reports shadows
look better. Preserve shadow light/projector and native capability/culling gates.
Runtime now verifies full stock SHA after normalizing only five display immediates
and label. Unknown builds pass through. INI toggles wired, Reset clears histories,
MinHook setup fails as a whole. Eight installer tests and affine tests pass.

2026-10-06 23:30: Actual final rhythm DrawUP trace proves spikes. Steady native phase
1109->1110->1120->1111; early abs(audio-native)>4 return zeroes offset and causes
19–23pixel single-frame center-note jumps. Separate later sample also reproduces
13–15pixel fallback jumps. Audio getter7c1bd0 reads several audio-thread-updated
position/segment fields; no writes in getter, but coherent snapshot is unproven.
Senior review explicitly invoked on gpt-6-astra xhigh; effective settings verified.
App-specific Proton overrides added for game.exe/gurumin.exe only; both keys absent
before changes, backed up as structured prior-state record outside source. Queries
verify native,builtin. Use Proton runinprefix for registry/debug tools, not run
(Steam stub waits alongside the existing game).

2026-10-06 23:45: Adopted Astra review's narrow adapters. RhythmClock rejects
innovation >4 against prediction while continuing QPC advancement; accepted PLL
correction capped at10% nominal velocity, no innovation reset. Reacquisition requires
3 distinct advancing observations over0.15s. Delta is cyclic480 relative to actual
native phase, so outlier-native positions cancel. Replay tests pass including bad,
missing/repeated observations, positive bounded velocity and discontinuities.
Native pause-state wiring still needs in-game verification; explicit pause behavior
is tested in the pure observer but ordinary HUD currently supplies active playback.

Mode15 draw argument adapter supplies validated interpolated scene VP and rigid
world before native WVP construction. Retains native branch, original identities,
and suppresses duplicate constant filtering. Shader diagnostic logs attribute modes.
Attachments now use actual successful c11 native/submitted snapshots, exact pass VP,
and unique nearest ancestor bone ownership. Native fallback/mixed result retained;
ambiguous ownership declines transport. Actual source pairing regression EXE passes
under Proton (fallback, same-tick updates, multiple candidates/pass separation/stale
frame). Projected-quad77fbe0 scope uses actual viewport W/H, forwards native vertices
without UI corrections, then restores dimensions. These fixes need visual acceptance
for face/cane, gates, door/tree animation and moving smoke; no claims of completion.
Review disposition recorded in SENIOR_REVIEW.md.

2026-10-06 23:55: User confirms rhythm bar nowlooks good; eyes still judder.
Runtime actual face mode31 (not15), body/hair/cane33, alpha worldobjects30.
RE mode31 branch7845da: uploads transpose(world) to c4..7 at7846af and VPc0 at
78471a. Old c8-three-vector correction never touches this transform. Mode30 branch
784bc1 uploads transpose(world) c8 FOURvectors at784d89, also missed n==3 test.
Extend the existing native draw argument adapter to verified modes30/31, before
all original constant assembly. Preserve scene camera validation/light rejection
and skip duplicate filtering. No extra constant patch and no palette rewrite.

2026-10-07: User aftermode31 adapter: eyes somewhatbetterbutstillwiggle;
doors/canevibrate, smokeoffset. Reopened narrow render-boundary decision: removing
mode whitelist from existing validated scene-draw argument adapter, since other
modes assemble different constant layouts. All transforms originate same world/VP
parameters, identity/light rejection retained. World shader scope temporarily
uses actual viewport nativeW/H and bypasses UI DrawUP processing, then restores
uniform UI canvas. This preserves existing engine and interpolation histories.
Native source test now covers modes8/15/30/31/33 and temporary world-canvas scope
restoration, plus actual-palette pairing cases. Passed under Proton. New non-HUD
DrawUP caller probes will attribute any remaining smoke path rather than assuming
77fbe0 is its source. Latestbuild installed/testlaunch; visualacceptance pending.

Follow-up cleanup: draw-entry adapter restricted to actual main scene/full or
menu-shrunken viewport. Secondary256 targets cannot feed the main-camera history.
Removed old per-register c0/c8 filtering (duplicated policy), retaining only c11
bone submission interception and early shadow receiver VP substitution. Existing
interpolation helpers remain the single matrix policy. Diagnostic DrawUP source
counts now distinguish viewport dimensions, so early minimap calls cannot hide a
later fullscreen particle call. Change built/staged; runtime install pending.

Particle trace identifies actual non-HUD batched TRIANGLELIST DrawUP return77f14d,
inputcanvas1920 at viewport3440. Submitter77ee90 takes pretransformed vertices,
texture receiverECX and2stackargs ret8. Its world-effect caller725f40 is thiscall,
ECXeffectcontroller,3DWORD stackargs ret12. Call5d49df supplies firstsceneVPDA7900,
secondDA9b00 billboardaxes, thirdcategory1. Hook generation+submission at725f40:
only non-HUD mainviewport, actual nativeW/H duringgeneration, exact rendered VP
replacement whenmatchescurrentcameraNative, scoped bypassUI vertexprocessing.
HUD/secondarytargets unchanged; axes/category/receiver unchanged. This fixes the
concrete batch scale/translation mismatch without reconstructing particles.

Currentdesktop changed to2560x1440~74.89Hz DP-5,gameclient2560vsbackbuffer3440.
Askeduserwhethertestingdifferentmonitor ortargetremains3440x1440@175; no display
mode changes made by agent. Preserve originaltarget until clarified.

Particle/main-viewport cleanup tests passed under Proton, including preserve
receiver/category/axes, filtered VP in main world scope, restoring UIwidth/depth,
and declining HUD/secondarytarget scopes. Installed and launching world-particle
build with original3440target retained pending optional display clarification.

## 2026-10-07: original launcher, presentation cap and Steam Input orbit

The user confirmed the preceding judder fixes. New work preserves that renderer
and its native 30 Hz simulation reference. Astra (`gpt-6-astra`, verified xhigh)
reviewed the plan; recommendations and disposition are in SETTINGS_PLAN.md.

Verified original launcher: MFC main resource 102, Start ID3 / Close ID1 /
Cancel ID2 / Reset ID1023, graphics page resource130 / resolution combo1006.
CreateDialogIndirectParamA import RVA4479a8 is called at VA401D7F (five stack
arguments, stdcall), after which native OnInit has initialized all child pages.
The loader-lock bootstrap only swaps this name-verified import; full file hash
verification, configuration and UI calls occur afterward, outside DllMain.

Native Start/Close handlers VA6E90D0/6E9160 run gamepad/keyboard validators before
calling virtual OnOK. CDialog::OnOK VA401B07 runs UpdateData(1), then
CDialog::EndDialog VA4017C0 with IDOK. This ends the MFC modal loop before the
EndDialog import at VA4017E7 (return RVA17ED). Cancel uses IDCANCEL. The adapter
stages settings, temporarily translates resolution to valid legacy index5, and
commits only at that verified acceptance call. Rejection restores the extended
selection. Atomic replacement preserves unrelated INI preferences. The apparent
save helper VA6EFC30 is a no-op in this Steam build; acceptance does not rely on
that helper as proof of persistence.

Width/Height are authoritative for every saved modern choice. Absent keys retain
installed/native behavior; installed slot5 immediates are read with memcpy.
Direct3DCreate9 occurs before device/resource creation, after original startup's
mode switch. The adapter sets output VA908B0C/10, square target edge VA21E480C,
and the native dimension-dependent aspect VA908B20. Checked allocator operands
RVA31C41E/31C432 are updated together, with rollback on failure. Capability and
fullscreen-mode checks select a coherent fallback; borderless output fits the
desktop. D3D device failure retries desktop windowed with matching target limits.
Runtime allocation failure after device creation remains a live validation risk
for the highest modes; caps alone do not prove available memory.

FrameCap0 follows native VSync, -1 is uncapped, numeric caps request immediate
presentation and wait after successful Present on an absolute QPC schedule.
Over-budget rendering receives no additional full-period delay; the next frame
rebases without burst catch-up. Native tick/sound/input timing is not patched.

Camera references (all production offsets are RVAs): native poll VA7AE330 is
cdecl with one state pointer (plain ret confirmed at VA7AE5B4); selected XInput
reader/index/mode are VA22280CC/E4/D4. Right-stick values occupy joystick-state
indices3/4 with neutral0x8000. One private success-checked sample uses the native
reader/device; only eligible gameplay consumes those axes. Buttons/left stick,
menus and scripted input retain native handling. Steam Input exposes the user's
DualSense/Steam Controller through this existing path when configured as a gamepad.

Camera VA712F90 is cdecl with two Vec4 output pointers (caller VA5A98BD cleans8).
Manual blocker VA735670 returns nonzero when blocked. Desired eye/target are
VA217B5B0/1EE4900. Native yaw rotates XY, retaining Z; integration occurs once per
native simulation tick, with short missed-tick intervals accounted for and long
suspensions rebased. Native manual flag VA1664454 is set2 only on accepted stick
movement. The original updater still owns follow, collision and view construction.

Persistent user pitch is added only at the five direct native pitch callsites:
VA71415F,7141E2,7145B6,7149CC,714B7B => return RVAs314164,3141E7,3145BB,
3149D1,314B80. Rotation helper VA779200 has ECX Vec4 receiver, one stack float,
ret4. The detour uses the established fastcall spare-EDX bridge. Native correction
VA1664440 and its 0.7 recovery remain unchanged: calculations receive U+C or
U+0.7*C, never 0.7*(U+C). Main collision testing remains outside the native C!=0
recovery branch. The three optional camera hooks form a separate rollback group;
a failure does not disable working rendering hooks.

A second strong implementation review found that tying camera ownership to HUD
render availability would discard user pitch after device reset. HUD availability
now gates input only. Focus/menu/disconnect/reset suspend integration while
retaining angle; native scripted takeover discards it. A native fixture regression
checks this distinction and the actual pitch ABI/callsite scope.

Verification: warning-free x86 DLL and expanded Windows fixture compilation;
portable mode/deadzone/orbit/pacer behavior tests; existing affine/rhythm tests;
eight transactional installer integration tests. Latest Windows fixture and
real-game launcher/controllers/cap timing could not run in the current socket-
restricted shell, including after user granted full permission. No new visual,
controller or 175 Hz acceptance claim is made from static/portable checks.

Additional native input gate: yaw-handler entry VA612E1F branches out when
VA B91EC0 is zero. The orbit input predicate also respects that active-control
gate, while retaining angle when input alone is suspended. This avoids extending
manual camera input into contexts the original caller excludes.

## 2026-10-07 live launcher/save and cap verification

Desktop access is now available. Fixed erroneous treatment of the profile-cache
flush's expected zero return as a failed write (Microsoft Win32 API contract).
Individual key/copy failures still abort; the temporary file is explicitly
flushed before same-directory atomic replacement. Installed save fix.

Expanded native fixture passed in Proton: actual x86 pitch bridges and all five
callsite scopes, camera reset/script ownership, accepted and cancelled settings,
and unknown preference preservation. Tolerances replace exact decimal float
comparisons in new camera checks for x87 excess precision.

Observed launcher save: Width=3440, Height=1440, FrameCap=175, FreeCamera=1,
InvertCameraY=0, existing motion/rhythm preferences retained. Actual D3D9 device
3440x1440, internal RT and depth resources3440x3440, scene viewport3440x1440.
Five recent600-frame intervals measured174.93–175.03 FPS and30.03–30.05 simulation
Hz. Live selected-device XInput samples drive nonzero yaw and pitch.
New title/fade coverage and skybox report remain under investigation.

## Reviewed graphics batch, 2026-10-07

Astra xhigh review completed; dispositions and test plan in GRAPHICS_PLAN.md.
Implemented alternate-native-projection sky camera adapter, proportional
full-width title pattern, exact solid fade/iris coverage, post-character
Falcom opening wings, independent UI/world asset sampling, optional shadow
resolution, and scene-only Edge AA Low/High. More Graphics modal has a local
draft and uses the existing parent accepted-save path.

Offline portable shadow/settings checks, eight installer checks and native
camera/render/save fixture pass. The AA worker's real Wine D3D9 readback fixture
verified Low/High edge filtering, tint/alpha, content bounds, one fallback draw,
post-AA HUD state and PointSampler restoration.

First combined test: launcher/modal/inversion controls visible, Cancel discarded
edits, Apply retained the draft without saving, and parent acceptance saved
1024 shadows/Edge AA High/native UI+world filtering. Title scrolling pattern
visibly covers the screen; opening wings execute after character submission.
Gameplay logs prove Edge AA High runs before upper HUD with successful state
restoration; HUD remains proportional.

Desktop currently exposes only 2560x1440 at 75 Hz, so the requested 3440x1440 was
preserved while output correctly fell back to 2560x1440. Ultrawide hardware
validation awaits its availability.

The first run exposed two corrections: the shadow allocator supplies a native
sentinel pointer (not null), so the added null-data guard was removed; the
additional wildcard camera cast is restricted to a vertical orbit offset after
seeing over-contraction near furniture at the ordinary native pitch. Native
collision handles the unmodified pitch. These corrections require the follow-up
combined test; sky motion, floor behavior and high shadow detail remain pending.

### Combined-test follow-up and camera entry

The follow-up run corrected shadow allocation/binding and validated all ten
1024px targets; the user confirms shadows look good. The ultrawide is now back:
actual 3440x1440 output/viewport and 3440px square scene allocation were verified.
Alternate sky passes use the native alternate projection and shared camera
interpolation. The user confirms orbit works otherwise, but its first outdoor
view imported the wrong orientation and the custom AA was not visibly apparent.

The authored outdoor camera now runs until fresh nonzero right-stick input.
Separate takeover ownership survives neutral/menu/disconnect; scene/script reset
clears it and invalidates scene-tagged input. Native hook regression checks pass,
including zero first-input delta and no stale cross-area takeover. New
CameraInteriorsOnly preference and launcher checkbox preserve default behavior;
its reliable area classifier and authentic scene-only FXAA are in progress.

### Final programming batch

Authentic licensed FXAA3.11 replaces the provisional edge filter. Frozen
3440x1440 game native/High readbacks show diagonal-edge smoothing before the HUD;
all ten dynamic shadow targets are1024px in the tested profile. World simulation
remains30Hz while presentation sustains175FPS. Interior classifier is now based
on decoded native map ID/asset pairs, and the requested three camera defaults
are enabled. Output-only outdoor offsets were replaced with by-value view
substitution to prevent native carry feedback; authored anchors continue moving.
Outdoor extra collision is restricted to active lowering. Numeric caps now
honor native VSync, and popup actor anchors/layout retain subpixel motion.
Manual book camera passes are isolated from cached gameplay interpolation.
Final production build and combined gameplay acceptance pending.

### First-goggles reward crash correction

User reproduced an instant close during the first goggles reward preview, before
equipping. A local first-chance access-violation trace identified native7A58C1
reading texture+1C with ECX/ESI=1440. Native77FBE0 immediately binds its ECX
texture receiver through7A58B0 and cleans six stack DWORDs. The proxy incorrectly
declared it stdcall, losing ECX while changing current viewport dimensions.
Corrected to thiscall/fastcall receiver bridge; all six arguments retain their
bits, including the mode-dependent fourth argument. Added actual x86 receiver,
argument, width/height scope and restoration regression coverage.

A bounded Terra review also found persistent raw skin-controller reads. Bone
owner identities are now snapshotted at successful palette submission, and
attachment ancestry and optional material arrays are checked before reads.
Regression coverage includes a poisoned former controller, unreadable parent
and secondary texture array. Existing interpolation remains active. Diagnostic
fault trace returns CONTINUE_SEARCH and never changes exception behavior.
First-goggles live revalidation pending.

### Audited reward live validation

Astra xhigh inspected all native hook contracts and found one further issue:
shadowBind returned bool/AL where native consumers test DWORD EAX. Corrected to
unsigned and validated independent full-register/stack forwarding. Added cycle,
unterminated-chain, unreadable owner/material, same-frame reparent/replacement,
mode/vertex-data/nested/zero-viewport quad, sprite13DWORD and shadow14DWORD
fixtures. All pass; portable shadow and nine installer tests pass; compile clean.

The audited production run executed the formerly failing goggles quad, completed
the reward and continued outdoors with goggles equipped. No new fault trace was
appended; steady presentation remains about175FPS with30Hz simulation, including
2048px shadows and nearest UI/world. Outdoor Manual camera fix was user-confirmed.
Diagnostic logging is disabled for future launches; user preferences preserved.

### Accessory shadow and cinematic coverage follow-up

User confirmed first-goggles reward completion, then reported goggles-only shadow
judder, ground-shadow stripes, incomplete cinematic bars and transition black.
Native capture calls modelDraw at76984E, temporarily mode16. That pass previously
kept rigid equipment matrices at30Hz while skin palettes and visible equipment
were interpolated. The shared attachment/world adapter now applies within that
exact subtree, leaving light VP native and retaining pass-specific palette guards.

The visible scene and receiver previously calculated the same camera VP through
two arithmetic paths. An independent x86 reproduction shows a depth difference
of one float ULP. They now share one materialized sparse-formula result; stationary
world matrices also retain native bytes. No blanket depth offset is added.

Verified cutscene bars in upper/lower HUD and actor-script rendering, plus
actor-script fades, now cover the full output width. Regression checks preserve
native Y/color/depth/opacity and reject unrelated rectangles. Live revalidation
pending; authored camera turn judder in the first ruins is being investigated.

The user additionally reported camera-turn judder with free camera disabled in
the first ruins. The shared interpolation guard measured view-space translation,
which changes strongly during rotation far from the map origin. With Astra's
review, smoothCamera now inverts the validated native view to camera-to-world,
uses the same pose history and physical-eye teleport guard, then inverts back.
This corrects authored-camera rendering without changing native camera controls.

Combined candidate verification: production and native test compile clean;
Windows native fixture, portable affine/shadow checks and all9 installer tests
pass. Installed DLL matches production. Live3440x1440 confirms goggles shadow
mode16 and visible mode33 both corrected, with59 models using the corrected
capture path. Ten actual2048 targets retained; steady presentation174.62FPS with
29.98Hz simulation. Current sampled ruins shadow appears solid without the prior
fine stripes, but full motion/cutscene/transition acceptance awaits user feedback.
No new fault trace appended. Diagnostic logging disabled for future launches;
current verification process remains running for the user's visual checks.
