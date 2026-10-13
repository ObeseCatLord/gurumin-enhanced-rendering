#pragma once
// VR render state shared by the existing draw-entry hooks in display.cpp and
// the VR frame logic in vr.hpp. A "region" is one eye view inside the
// side-by-side pass target; foveated rendering can later add more regions.
struct VrRegion {
  D3DVIEWPORT9 viewport{};
  float view[16]{};   // Game row-vector view matrix for this eye.
  float vp[16]{};     // view * projection for the main scene pass.
  float vpAlt[16]{};  // view * projection for the alternate (sky) pass.
};
static bool vrSession;          // VR requested for this launch.
static bool vrPass;             // Inside the VR scene traversal.
static bool vrHudRedirected;    // HUD draws go to the VR overlay target.
static bool vrAxesCompatible;   // Native view layout matches our eye views.
static int vrRegionCount;
static VrRegion vrRegions[4];
static int vrRegion = -1;       // Region of a duplicated draw (single pass).
static int vrFixedRegion = -1;  // Region of a whole traversal (two pass).
static unsigned vrUnscopedDraws; // Scene-pass draws outside known entries.
static int vrCurrentRegion() { return vrRegion >= 0 ? vrRegion : vrFixedRegion; }
// Single pass: issue `draw` once per region with that region's viewport.
template <class F> static unsigned vrEachRegion(F draw);
static bool vrSceneMatrix(const float *matrix);
static const float *vrRegionMatrix(int region, const float *nativeMatrix);
static bool vrBlit(IDirect3DDevice9 *, D3DPRIMITIVETYPE, UINT, const void *, UINT);
static void vrBeforePresent(IDirect3DDevice9 *);
static void vrAfterPresent();
static void vrDeviceCreated(IDirect3DDevice9 *);
static void vrDeviceLost();
static bool vrSceneRoot(void (__cdecl *native)());
static void vrInputOverlay(int *state);
static int vrFadeDepth, vrBarDepth; // Native fullscreen fade / cinematic bars.
static HRESULT vrFadeEyes(IDirect3DDevice9 *, D3DPRIMITIVETYPE, UINT, const void *, UINT);
static IDirect3DSurface9 *vrRedirectTarget(DWORD index, IDirect3DSurface9 *target);
static bool vrMovement(uintptr_t caller, float stickX, float stickY,
                       float *outX, float *outY);
