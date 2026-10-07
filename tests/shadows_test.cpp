#define GURUMIN_SHADOWS_TEST

#include <cassert>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <vector>

struct TestSettings { int shadowResolution = 256; };
static TestSettings modernSettings;
static uintptr_t gameBase;
template <class T> static T &game(uintptr_t rva) {
  return *reinterpret_cast<T *>(gameBase + rva);
}
static void log(const char *, ...) {}

struct HookCall { void *target; void *detour; };
static std::vector<HookCall> created, enabled, disabled, removed;
static int createResult = 0, enableResult = 0;
static int createCalls, failCreateOn;
using MH_STATUS = int;
MH_STATUS MH_CreateHook(void *target, void *detour, void **original) {
  created.push_back({target, detour});
  if (original) *original = nullptr;
  if (++createCalls == failCreateOn) return 1;
  return createResult;
}
MH_STATUS MH_EnableHook(void *target) {
  enabled.push_back({target, nullptr});
  return enableResult;
}
MH_STATUS MH_DisableHook(void *target) {
  disabled.push_back({target, nullptr});
  return 0;
}
MH_STATUS MH_RemoveHook(void *target) {
  removed.push_back({target, nullptr});
  return 0;
}

#include "../src/shadows.hpp"

D3DVIEWPORT9 actualViewport{};

struct FakeSurface final : IDirect3DSurface9 {
  D3DSURFACE_DESC desc{};
  HRESULT GetDesc(D3DSURFACE_DESC *out) override {
    *out = desc;
    return S_OK;
  }
  unsigned long Release() override { return 1; }
};
struct FakeDevice final : IDirect3DDevice9 {
  FakeSurface *depth = nullptr;
  D3DVIEWPORT9 viewport{};
  HRESULT GetDepthStencilSurface(IDirect3DSurface9 **out) override {
    *out = depth;
    return depth ? S_OK : -1;
  }
  HRESULT GetViewport(D3DVIEWPORT9 *out) override {
    *out = viewport;
    return S_OK;
  }
};
static FakeDevice *bindingDevice;
static int boundWidth, boundHeight;
static unsigned __thiscall bindNative(void *, int width, int height, int) {
  boundWidth = width;
  boundHeight = height;
  bindingDevice->viewport = {0, 0, unsigned(width), unsigned(height), 0, 1};
  return true;
}

static void clearHooks() {
  created.clear(); enabled.clear(); disabled.clear(); removed.clear();
  createResult = enableResult = MH_OK;
  createCalls = failCreateOn = 0;
}

int main() {
  std::vector<unsigned char> image(0x1da4000);
  gameBase = reinterpret_cast<uintptr_t>(image.data());

  modernSettings.shadowResolution = 256;
  assert(shadowRequestedEdge() == 256);
  assert(!shadowDepthSize(640, 480, kShadowDepthAllocateReturnRva).enlarged);
  modernSettings.shadowResolution = 2048;
  auto enlarged = shadowDepthSize(1440, 1024, kShadowDepthAllocateReturnRva);
  assert(enlarged.enlarged && enlarged.width == 2048 && enlarged.height == 2048);
  auto unrelated = shadowDepthSize(1440, 1024, 0x3a4159);
  assert(!unrelated.enlarged && unrelated.width == 1440 && unrelated.height == 1024);
  modernSettings.shadowResolution = 777;
  assert(shadowRequestedEdge() == 256);
  assert(shadowNameIndex("shadow0") == 0 && shadowNameIndex("shadow9") == 9);
  assert(shadowNameIndex("shadow10") == -1 && shadowNameIndex("mirror") == -1);

  // Creation failure does not remove or disable any pre-existing core hooks.
  clearHooks();
  createResult = 1;
  assert(!installShadowHooks());
  assert(created.size() == 1 && removed.empty() && disabled.empty());

  // A second-create failure removes only this module's first hook.
  clearHooks();
  failCreateOn = 2;
  assert(!shadowHooksInstalled);
  assert(!installShadowHooks());
  assert(created.size() == 2 && removed.size() == 1 && disabled.empty());

  // An enable failure rolls back only this module's two hooks.
  clearHooks();
  enableResult = 1;
  assert(!installShadowHooks());
  assert(created.size() == 2 && enabled.size() == 1);
  assert(disabled.size() == 2 && removed.size() == 2);
  assert(!shadowHooksInstalled);

  clearHooks();
  assert(installShadowHooks());
  assert(shadowHooksInstalled && created.size() == 2 && enabled.size() == 2);
  assert(created[0].target == reinterpret_cast<void *>(gameBase + kShadowAllocateRva));
  assert(created[1].target == reinterpret_cast<void *>(gameBase + kShadowBinderRva));

  // The binder uses all ten verified resource identities, preserves the outer
  // canvas for the next native restore, and installs a full square viewport.
  shadowReset();
  modernSettings.shadowResolution = 512;
  FakeSurface target, depth;
  target.desc.Width = target.desc.Height = 512;
  depth.desc.Width = depth.desc.Height = 3440; // Common depth can be larger.
  FakeDevice device;
  device.depth = &depth;
  bindingDevice = &device;
  shadowGame<IDirect3DDevice9 *>(kRendererDeviceRva) = &device;
  shadowGame<IDirect3DSurface9 *>(0x1da35f4) = &depth;
  for (unsigned i = 0; i != shadowHandles.size(); ++i) {
    shadowHandles[i] = int(i);
    char name[16]; snprintf(name,sizeof(name),"shadow%u",i);
    strcpy(reinterpret_cast<char*>(gameBase+0x128c7d0+i*0x40),name);
    *reinterpret_cast<IDirect3DSurface9 **>(
        gameBase + kShadowResourceTableRva + i * 0x38u + 4) = &target;
  }
  shadowReadyMask = 0x3ff;
  shadowBind = bindNative;
  shadowGame<int>(kNativeCanvasWidthRva) = 1920;
  shadowGame<int>(kNativeCanvasHeightRva) = 1080;
  shadowGame<int>(kNativeCurrentWidthRva) = 1920;
  shadowGame<int>(kNativeCurrentHeightRva) = 1080;
  shadowGame<int>(kSavedCanvasActiveRva) = 0;
  assert(shadowBindHook(shadowResourceForHandle(0), nullptr, 256, 256, 0));
  assert(boundWidth == 512 && boundHeight == 512);
  assert(shadowGame<int>(kNativeCanvasWidthRva) == 1920);
  assert(shadowGame<int>(kNativeCanvasHeightRva) == 1080);
  assert(shadowGame<int>(kSavedCanvasWidthRva) == 1920);
  assert(shadowGame<int>(kSavedCanvasHeightRva) == 1080);
  assert(actualViewport.Width == 512 && actualViewport.Height == 512);
  shadowReset(true);
  assert(shadowReadyMask==0);
  assert(shadowTracksResource(shadowResourceForHandle(0),512));
  depth.desc.Width=256;
  assert(!shadowBindHook(shadowResourceForHandle(0),nullptr,512,512,0));
  assert(shadowFallback && shadowGame<int>(kDynamicShadowGateRva)==0);
}
