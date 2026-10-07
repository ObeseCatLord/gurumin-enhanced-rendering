#pragma once

// Bounded dynamic-shadow target adapter for the verified Gurumin 1.4 build.
// This header is intentionally included only after display.cpp's renderer
// globals (`gameBase`, `modernSettings`, `actualViewport`, and `log`) exist.

#include <array>
#include <algorithm>
#include <cstdint>
#include <cstring>

#ifdef GURUMIN_SHADOWS_TEST
#include <cstdarg>
using UINT = unsigned;
using DWORD = unsigned long;
using BOOL = int;
using HANDLE = void *;
using HRESULT = long;
#define WINAPI
#define __cdecl
#define __thiscall
#define __fastcall
constexpr HRESULT S_OK = 0;
inline bool FAILED(HRESULT result) { return result < 0; }
enum D3DMULTISAMPLE_TYPE { D3DMULTISAMPLE_NONE = 0 };
struct D3DSURFACE_DESC {
  UINT Format = 0, Type = 0, Usage = 0, Pool = 0, MultiSampleQuality = 0;
  D3DMULTISAMPLE_TYPE MultiSampleType = D3DMULTISAMPLE_NONE;
  UINT Width = 0, Height = 0;
};
struct D3DVIEWPORT9 {
  DWORD X = 0, Y = 0, Width = 0, Height = 0;
  float MinZ = 0, MaxZ = 1;
};
struct IDirect3DSurface9 {
  virtual HRESULT GetDesc(D3DSURFACE_DESC *) = 0;
  virtual unsigned long Release() = 0;
};
struct IDirect3DDevice9 {
  virtual HRESULT GetDepthStencilSurface(IDirect3DSurface9 **) = 0;
  virtual HRESULT GetViewport(D3DVIEWPORT9 *) = 0;
};
using MH_STATUS = int;
constexpr MH_STATUS MH_OK = 0;
extern MH_STATUS MH_CreateHook(void *, void *, void **);
extern MH_STATUS MH_EnableHook(void *);
extern MH_STATUS MH_DisableHook(void *);
extern MH_STATUS MH_RemoveHook(void *);
extern D3DVIEWPORT9 actualViewport;
#else
#include "MinHook.h"
#include <d3d9.h>
#endif

// Native VAs below are expressed as RVAs.  Do not broaden these identities to
// similarly shaped allocations or binders: mirror/minimap targets share the
// allocator and D3D's depth factory also serves unrelated surfaces.
static constexpr uintptr_t kShadowAllocateRva = 0x31c5b0;
static constexpr uintptr_t kShadowAllocateReturnRva = 0x334403;
static constexpr uintptr_t kShadowBinderRva = 0x3a4bf0;
static constexpr uintptr_t kShadowDepthAllocateReturnRva = 0x3a4158;
static constexpr uintptr_t kShadowResourceTableRva = 0x198f6a0;
static constexpr uintptr_t kRendererDeviceRva = 0x1da3540;
static constexpr uintptr_t kNativeCanvasWidthRva = 0x508b0c;
static constexpr uintptr_t kNativeCanvasHeightRva = 0x508b10;
static constexpr uintptr_t kNativeCurrentWidthRva = 0x508b14;
static constexpr uintptr_t kNativeCurrentHeightRva = 0x508b18;
static constexpr uintptr_t kSavedCanvasWidthRva = 0x1da35c0;
static constexpr uintptr_t kSavedCanvasHeightRva = 0x1da35c4;
static constexpr uintptr_t kSavedCanvasActiveRva = 0x1da35c8;
static constexpr uintptr_t kDynamicShadowGateRva = 0x508a10;

template <class T> static T &shadowGame(uintptr_t rva) {
  return game<T>(rva);
}

struct ShadowDepthExtent {
  UINT width;
  UINT height;
  bool enlarged;
};

// The setting deliberately accepts only reviewed square edges.  Invalid or
// absent values preserve the exact native 256x256 path.
static UINT shadowRequestedEdge() {
  switch (modernSettings.shadowResolution) {
  case 256:
  case 512:
  case 1024:
  case 2048:
    return static_cast<UINT>(modernSettings.shadowResolution);
  default:
    return 256;
  }
}

static bool shadowHighResolutionRequested() { return shadowRequestedEdge() > 256; }

// The sole native CreateDepthStencilSurface call that populates common depth
// is 0x7a4155 -> return RVA 0x3a4158.  Keep its depth square and at least as
// large as both the native scene edge and the selected shadow edge.
static ShadowDepthExtent shadowDepthSize(UINT requestWidth, UINT requestHeight,
                                         uintptr_t returnRva) {
  if (returnRva != kShadowDepthAllocateReturnRva ||
      !shadowHighResolutionRequested())
    return {requestWidth, requestHeight, false};
  const UINT edge = std::max({requestWidth, requestHeight, shadowRequestedEdge()});
  return {edge, edge, edge != requestWidth || edge != requestHeight};
}

static int shadowNameIndex(const char *name) {
  if (!name || std::strncmp(name, "shadow", 6) != 0 ||
      name[6] < '0' || name[6] > '9' || name[7] != '\0')
    return -1;
  return name[6] - '0';
}

using ShadowAllocateFn = int(__cdecl *)(const char *, int, int, void *, int, int);
using ShadowBindFn = unsigned(__thiscall *)(void *, int, int, int);

static ShadowAllocateFn shadowAllocate;
static ShadowBindFn shadowBind;
static std::array<int, 10> shadowHandles{};
static unsigned shadowReadyMask;
static bool shadowFallback;
static bool shadowHooksInstalled;

static void shadowReset(bool keepHandles = false) {
  if(!keepHandles) shadowHandles.fill(-1);
  shadowReadyMask = 0;
  shadowFallback = false;
}

static IDirect3DSurface9 *shadowSurface(void *resource) {
  return resource ? *reinterpret_cast<IDirect3DSurface9 **>(
                        static_cast<unsigned char *>(resource) + 4)
                  : nullptr;
}

static bool shadowSurfaceIsEdge(void *resource, UINT edge,
                                D3DSURFACE_DESC *out = nullptr) {
  IDirect3DSurface9 *surface = shadowSurface(resource);
  D3DSURFACE_DESC desc{};
  if (!surface || FAILED(surface->GetDesc(&desc)) || desc.Width != edge ||
      desc.Height != edge)
    return false;
  if (out)
    *out = desc;
  return true;
}

static void *shadowResourceForHandle(int handle) {
  if (handle < 0 || handle >= 1600)
    return nullptr;
  return reinterpret_cast<void *>(gameBase + kShadowResourceTableRva +
                                  uintptr_t(handle) * 0x38u);
}

static bool shadowTracksResource(void *resource, UINT edge) {
  // Reset recreates the native COM surfaces without necessarily re-running
  // the named allocator. Retain handles, but reacquire every resource and
  // validate its name and actual descriptor before trusting the set again.
  if(!shadowFallback && shadowReadyMask!=0x3ffu) {
    unsigned ready=0;
    for(unsigned i=0;i<10;++i) {
      int handle=shadowHandles[i];
      if(handle<0 || handle>=1600) break;
      auto name=reinterpret_cast<const char*>(gameBase+0x128c7d0+handle*0x40);
      if(shadowNameIndex(name)!=int(i) ||
         !shadowSurfaceIsEdge(shadowResourceForHandle(handle),edge)) break;
      ready|=1u<<i;
    }
    if(ready==0x3ffu) shadowReadyMask=ready;
  }
  if (shadowFallback || shadowReadyMask != 0x3ffu)
    return false;
  for (int handle : shadowHandles)
    if (resource == shadowResourceForHandle(handle))
      return shadowSurfaceIsEdge(resource, edge);
  return false;
}

static void shadowDisableDynamic(const char *reason) {
  if (shadowFallback)
    return;
  shadowFallback = true;
  shadowReadyMask = 0;
  // This is the engine's verified dynamic-shadow gate.  A partial high target
  // set is never used: every actor falls back through the native kage.bmp path.
  if (gameBase)
    shadowGame<int>(kDynamicShadowGateRva) = 0;
  log("Shadow high-resolution fallback: %s", reason);
}

static bool shadowDepthMatches(void *resource, UINT edge, bool bound = true) {
  D3DSURFACE_DESC target{};
  if (!shadowSurfaceIsEdge(resource, edge, &target))
    return false;
  IDirect3DDevice9 *device = shadowGame<IDirect3DDevice9 *>(kRendererDeviceRva);
  if (!device)
    return false;
  IDirect3DSurface9 *depth = nullptr;
  if(bound) {
    if (FAILED(device->GetDepthStencilSurface(&depth)) || !depth) return false;
  } else {
    depth=shadowGame<IDirect3DSurface9*>(0x1da35f4);
    if(!depth) return false;
  }
  D3DSURFACE_DESC depthDesc{};
  const HRESULT descResult = depth->GetDesc(&depthDesc);
  if(bound) depth->Release();
  return !FAILED(descResult) && depthDesc.Width >= edge &&
         depthDesc.Height >= edge &&
         depthDesc.MultiSampleType == target.MultiSampleType &&
         depthDesc.MultiSampleQuality == target.MultiSampleQuality;
}

static int __cdecl shadowAllocateHook(const char *name, int width, int height,
                                      void *pixels, int flag, int unknown) {
  if (!shadowAllocate)
    return -1;
  const int index = shadowNameIndex(name);
  const bool exactCaller = uintptr_t(__builtin_return_address(0))-gameBase == kShadowAllocateReturnRva;
  const UINT edge = shadowRequestedEdge();
  if (index < 0 || !exactCaller || width != 256 || height != 256 ||
      flag!=1 || unknown!=0 || edge == 256 ||
      shadowFallback)
    return shadowAllocate(name, width, height, pixels, flag, unknown);

  const int handle = shadowAllocate(name, int(edge), int(edge), pixels, flag, unknown);
  void *resource = shadowResourceForHandle(handle);
  // Allocator cache reuse can return a successful old 256 handle for a name.
  // Treat that as failure; do not claim the setting worked based on the name.
  if (handle >= 0 && shadowSurfaceIsEdge(resource, edge)) {
    shadowHandles[index] = handle;
    shadowReadyMask |= 1u << unsigned(index);
    log("Shadow target %s handle=%d actual=%ux%u readyMask=%x",name,handle,edge,edge,shadowReadyMask);
    return handle;
  }

  if (handle < 0) {
    // A failed native allocation has no registered cache entry, so the exact
    // native dimensions may be retried for storage before we disable dynamics.
    const int fallback = shadowAllocate(name, width, height, pixels, flag, unknown);
    shadowDisableDynamic(fallback >= 0 ? "high target allocation failed"
                                       : "high and native target allocation failed");
    return fallback;
  }
  shadowDisableDynamic("shadow-name cache did not contain the requested dimensions");
  return handle;
}

static unsigned __fastcall shadowBindHook(void *resource, void *, int width, int height,
                                      int inset) {
  if (!shadowBind)
    return false;
  const UINT edge = shadowRequestedEdge();
  if(shadowFallback) {
    auto offset=uintptr_t(resource)-(gameBase+kShadowResourceTableRva);
    if(offset<1600*0x38u && offset%0x38u==0 &&
       shadowNameIndex(reinterpret_cast<const char*>(gameBase+0x128c7d0+(offset/0x38u)*0x40))>=0) {
      shadowGame<int>(kDynamicShadowGateRva)=0;
      return false;
    }
  }
  if (edge == 256 || !shadowTracksResource(resource, edge))
    return shadowBind(resource, width, height, inset);

  // The native binder clamps against these canvas globals.  Temporarily make
  // that clamp square, then repair the native saved outer dimensions so the
  // following scene restore returns to the same canvas it entered with.
  const int oldCanvasWidth = shadowGame<int>(kNativeCanvasWidthRva);
  const int oldCanvasHeight = shadowGame<int>(kNativeCanvasHeightRva);
  const int oldCurrentWidth = shadowGame<int>(kNativeCurrentWidthRva);
  const int oldCurrentHeight = shadowGame<int>(kNativeCurrentHeightRva);
  const bool savedOuterState = shadowGame<int>(kSavedCanvasActiveRva) == 0;
  if (!shadowDepthMatches(resource, edge, false)) {
    shadowDisableDynamic("common depth is not an adequate compatible shadow surface");
    return false;
  }
  shadowGame<int>(kNativeCanvasWidthRva) = int(edge);
  shadowGame<int>(kNativeCanvasHeightRva) = int(edge);
  const unsigned result = shadowBind(resource, int(edge), int(edge), inset);
  shadowGame<int>(kNativeCanvasWidthRva) = oldCanvasWidth;
  shadowGame<int>(kNativeCanvasHeightRva) = oldCanvasHeight;
  if (!result) {
    shadowDisableDynamic("native target binder returned failure");
    return false;
  }
  if (savedOuterState) {
    shadowGame<int>(kSavedCanvasWidthRva) = oldCurrentWidth;
    shadowGame<int>(kSavedCanvasHeightRva) = oldCurrentHeight;
  }
  // Re-read the currently bound depth surface after the native call.  The
  // native binder does not expose its SetDepthStencilSurface HRESULT to us.
  if (!shadowDepthMatches(resource, edge)) {
    shadowDisableDynamic("native binder did not retain a compatible common depth");
    return false;
  }
  D3DVIEWPORT9 viewport{};
  IDirect3DDevice9 *device = shadowGame<IDirect3DDevice9 *>(kRendererDeviceRva);
  if (!device || FAILED(device->GetViewport(&viewport)) || viewport.Width != edge ||
      viewport.Height != edge) {
    shadowDisableDynamic("native target binder did not establish a square viewport");
    return false;
  }
  actualViewport = viewport;
  return true;
}

// Installs/enables only this module's two native hooks.  A failure rolls back
// only hooks created here, leaving the caller's core MinHook group untouched.
static bool installShadowHooks() {
  if (shadowHooksInstalled)
    return true;
  shadowReset();
  void *allocateTarget = reinterpret_cast<void *>(gameBase + kShadowAllocateRva);
  void *binderTarget = reinterpret_cast<void *>(gameBase + kShadowBinderRva);
  if (MH_CreateHook(allocateTarget, reinterpret_cast<void *>(shadowAllocateHook),
                    reinterpret_cast<void **>(&shadowAllocate)) != MH_OK)
    return false;
  if (MH_CreateHook(binderTarget, reinterpret_cast<void *>(shadowBindHook),
                    reinterpret_cast<void **>(&shadowBind)) != MH_OK) {
    MH_RemoveHook(allocateTarget);
    shadowAllocate = nullptr;
    return false;
  }
  if (MH_EnableHook(allocateTarget) != MH_OK || MH_EnableHook(binderTarget) != MH_OK) {
    MH_DisableHook(allocateTarget);
    MH_DisableHook(binderTarget);
    MH_RemoveHook(allocateTarget);
    MH_RemoveHook(binderTarget);
    shadowAllocate = nullptr;
    shadowBind = nullptr;
    return false;
  }
  shadowHooksInstalled = true;
  log("Shadow hooks installed: edge=%u", shadowRequestedEdge());
  return true;
}
