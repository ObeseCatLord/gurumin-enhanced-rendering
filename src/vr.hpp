// OpenXR VR mode, game side. Included by display.cpp after camera.hpp.
// The x64 host (GuruminVR\gurumin_vr_host.exe) owns OpenXR; this side renders
// both eyes in one scene traversal into a side-by-side pass target, captures
// the HUD separately and exchanges images, poses and controller state with
// the host through vr_ipc.hpp. All offsets are RVAs of the verified build.
#include "vr_ipc.hpp"
#include "vr_math.hpp"
#include <cwchar>

namespace vrconst {
constexpr uintptr_t MainVP = 0x9a7900, AltVP = 0x9a9c00, NativeView = 0x9a9b00;
constexpr uintptr_t MainProjection = 0x9ac940, AltProjection = 0x975d18;
constexpr uintptr_t PlayerPosition = 0x108a348; // actor[0] world translation
constexpr uintptr_t CameraEye = 0x1cf17a0, CameraTarget = 0x198f690;
constexpr uintptr_t SceneShrinkX = 0x1de8d58, SceneShrinkY = 0x1de8d5c;
constexpr uintptr_t UseGamepad = 0x1df37c8, XInputModule = 0x1e280d4,
                    DInputDevice = 0x1e280e8;
constexpr uintptr_t Tick = 0x1de47f4, Scene = 0xf32c78, MapName = 0x527110;
constexpr uintptr_t MenuOpen = 0x515ec4, Device = 0x1da3540;
} // namespace vrconst

struct VrConfig {
  gurumin::vr::RigConfig rig;
  float renderScale = 1;
  int maxEye = 3072;
  bool singlePass = true, drawAllZones = true, zeroCopy = true;
  bool desktopMirror = false; // Also present to the desktop window.
  float hudDistance = 1.4f, hudWidth = 1.5f;
  float screenDistance = 2.2f, screenWidth = 2.4f;
};
static VrConfig vrConfig;
static gurumin::vr::Channel vrChannel;
static gurumin::vr::PixelStore vrPixels;
static HANDLE vrHostProcess;
static std::wstring vrHostDirectory, vrHostExecutable, vrHostCommand;
static ULONGLONG vrHostLaunchTick;
static unsigned vrHostLaunches;
static unsigned vrEyeWidth, vrEyeHeight, vrHudWidth, vrHudHeight;
static ULONGLONG vrStartTick, vrHostSeenTick;

struct VrImages {
  IDirect3DTexture9 *texture[2][gurumin::vr::ImageCount]{};
  IDirect3DSurface9 *surface[2][gurumin::vr::ImageCount]{};
  IDirect3DQuery9 *fence[2]{};
  IDirect3DQuery9 *flush = nullptr; // Submits work when Present is skipped.
  IDirect3DSurface9 *readbackEye = nullptr, *readbackHud = nullptr;
  IDirect3DTexture9 *overlay = nullptr; // Full-size HUD capture target.
  IDirect3DSurface9 *overlaySurface = nullptr;
  uint32_t generation = 0;
  bool shared = false, ready = false;
  bool pending[2]{};
  uint64_t pendingSequence[2]{};
};
static VrImages vrImages;
static uint32_t vrGenerations;

// Per-frame state.
static int vrFrameSlot = -1, vrOrphanSlot = -1;
static gurumin::vr::Request vrFrameRequest, vrLastRequest;
static bool vrHaveRequest, vrPassRan;
static unsigned vrPassFrame = ~0u, vrRigFrame = ~0u;
static IDirect3DSurface9 *vrSceneSurface, *vrSavedTarget;
static float vrLastEye[2][16], vrLastEyeAlt[2][16];
static unsigned vrStatPasses, vrStatBlits, vrStatRedirects, vrStatWorld, vrStatScreen,
    vrStatRootCalls;
static bool vrLastEyesValid;

// Rig and input.
static gurumin::vr::Rig vrRig;
static gurumin::vr::AnchorFilter vrAnchor;
static gurumin::vr::Pose vrLastHead;
static bool vrHeadValid, vrRecenterRequested, vrChord, vrThumbsWereDown;
static float vrPadRight[2];
static double vrLastRigTime;
static int vrRigScene = -1;
static char vrRigMap[64];
static gurumin::vr::Vec3 vrPlayerPrevious, vrPlayerCurrent;
static unsigned vrPlayerTick = ~0u;
static double vrPlayerStamp;
static ULONGLONG vrLastInputTick;
static gurumin::FramePacer vrFallbackPacer;

static float vrIniFloat(const char *key, float fallback, float low, float high) {
  char text[64];
  GetPrivateProfileStringA("GuruminModern", key, "", text, sizeof(text), modernIni);
  if (!text[0])
    return fallback;
  char *end = nullptr;
  float value = std::strtof(text, &end);
  return end != text && std::isfinite(value) ? std::clamp(value, low, high) : fallback;
}
static void loadVrConfig() {
  vrConfig = {};
  if (!modernIniPath())
    return;
  auto readInt = [](const char *key, int fallback) {
    return int(GetPrivateProfileIntA("GuruminModern", key, fallback, modernIni));
  };
  auto &r = vrConfig.rig;
  r.unitsPerMeter = vrIniFloat("VRUnitsPerMeter", 100, 1, 10000);
  r.dioramaScale = vrIniFloat("VRDioramaScale", 12, 1, 200);
  r.lifeDistance = vrIniFloat("VRLifeDistance", 1.4f, 0, 20);
  r.lifeHeight = vrIniFloat("VRLifeHeight", .9f, -5, 20);
  r.dioramaDistance = vrIniFloat("VRDioramaDistance", .45f, 0, 5);
  r.dioramaHeight = vrIniFloat("VRDioramaHeight", .25f, -2, 5);
  r.snapTurn = readInt("VRSnapTurn", 0) != 0;
  r.diorama = modernSettings.vrScale == 1;
  r.invertX = modernSettings.invertX;
  r.invertY = modernSettings.invertY;
  constexpr float radians = gurumin::vr::Pi / 180;
  r.yawSpeed = modernSettings.cameraYawSpeed * radians;
  r.elevationSpeed = modernSettings.cameraPitchSpeed * radians * .66f;
  vrConfig.renderScale = vrIniFloat("VRRenderScale", 1, .25f, 2);
  vrConfig.maxEye = std::clamp(readInt("VRMaxEyeSize", 3072), 512, 3840);
  vrConfig.singlePass = readInt("VRSinglePass", 1) != 0;
  vrConfig.drawAllZones = readInt("VRDrawAllZones", 1) != 0;
  vrConfig.zeroCopy = readInt("VRZeroCopy", 1) != 0;
  vrConfig.desktopMirror = readInt("VRDesktopMirror", 0) != 0;
  vrConfig.hudDistance = vrIniFloat("VRHudDistance", 1.4f, .3f, 10);
  vrConfig.hudWidth = vrIniFloat("VRHudWidth", 1.5f, .1f, 10);
  vrConfig.screenDistance = vrIniFloat("VRScreenDistance", 2.2f, .3f, 20);
  vrConfig.screenWidth = vrIniFloat("VRScreenWidth", 2.4f, .1f, 20);
}

static bool vrHostConnected() {
  return vrChannel.shared && vrChannel.shared->hostPid && !vrChannel.shared->shutdown;
}

static bool vrLaunchHost() {
  std::vector<wchar_t> arguments(vrHostCommand.begin(), vrHostCommand.end());
  arguments.push_back(0);
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  // A null environment keeps Steam/Proton's prefix and runtime variables.
  if (!CreateProcessW(vrHostExecutable.c_str(), arguments.data(), nullptr, nullptr, FALSE,
                      CREATE_NO_WINDOW, nullptr, vrHostDirectory.c_str(), &startup,
                      &process))
    return false;
  CloseHandle(process.hThread);
  if (vrHostProcess)
    CloseHandle(vrHostProcess);
  vrHostProcess = process.hProcess;
  vrHostLaunchTick = GetTickCount64();
  ++vrHostLaunches;
  return true;
}
// A runtime started after the game (or a host that stopped) gets a new host.
static void vrSuperviseHost() {
  if (!vrHostProcess || WaitForSingleObject(vrHostProcess, 0) != WAIT_OBJECT_0 ||
      GetTickCount64() - vrHostLaunchTick < 10000 || vrHostLaunches > 360)
    return;
  {
    gurumin::vr::Lock lock(vrChannel, 20);
    if (!lock)
      return;
    auto &s = *vrChannel.shared;
    s.hostPid = s.hostReady = s.error = s.shutdown = 0;
    s.images.opened = 0;
    for (auto &slot : s.slot)
      if (slot.state != gurumin::vr::SlotState::Rendering)
        slot = {};
  }
  vrImages.pending[0] = vrImages.pending[1] = false;
  vrHostSeenTick = 0;
  if (vrLaunchHost() && vrHostLaunches <= 3)
    log("VR host restarted (attempt %u)", vrHostLaunches);
}

// Starts the channel and the host before the D3D device exists so the eye
// size can follow the runtime's recommendation.
static void vrStart() {
  static bool started;
  if (started || launcherWindow)
    return;
  started = true;
  vrSession = supported && (modernSettings.vrEnabled ||
                            gurumin::commandLineRequestsVr(GetCommandLineA()));
  if (!vrSession)
    return;
  loadVrConfig();
  wchar_t token[64];
  swprintf(token, 64, L"%lu-%llu", (unsigned long)GetCurrentProcessId(),
           (unsigned long long)GetTickCount64());
  if (!vrChannel.open(token, true)) {
    log("VR channel creation failed; VR disabled");
    vrSession = false;
    return;
  }
  wchar_t path[MAX_PATH];
  DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
  wchar_t *slash = length && length < MAX_PATH ? wcsrchr(path, L'\\') : nullptr;
  if (!slash || size_t(slash - path) + 40 >= MAX_PATH) {
    vrSession = false;
    return;
  }
  slash[1] = 0;
  std::wstring directory = std::wstring(path) + L"GuruminVR\\";
  std::wstring executable = directory + L"gurumin_vr_host.exe";
  if (GetFileAttributesW(executable.c_str()) == INVALID_FILE_ATTRIBUTES) {
    log("VR host GuruminVR\\gurumin_vr_host.exe missing; VR disabled");
    vrSession = false;
    vrChannel.close();
    return;
  }
  vrHostDirectory = directory;
  vrHostExecutable = executable;
  vrHostCommand = L"\"" + executable + L"\" --channel " + token;
  if (!vrLaunchHost()) {
    log("VR host launch failed error=%lu; VR disabled", (unsigned long)GetLastError());
    vrSession = false;
    vrChannel.close();
    return;
  }
  vrStartTick = GetTickCount64();
  // Wait briefly for the runtime's recommended size; SteamVR may need longer,
  // in which case the cached size is used and the host connects later.
  unsigned recommendedWidth = 0, recommendedHeight = 0;
  for (int i = 0; i < 300; ++i) {
    auto &s = *vrChannel.shared;
    if (s.hostReady && s.recommendedWidth && s.recommendedHeight) {
      recommendedWidth = s.recommendedWidth;
      recommendedHeight = s.recommendedHeight;
      break;
    }
    if (s.error || WaitForSingleObject(vrHostProcess, 0) == WAIT_OBJECT_0)
      break;
    Sleep(20);
  }
  unsigned width = 0, height = 0;
  if (recommendedWidth && recommendedHeight) {
    width = unsigned(recommendedWidth * vrConfig.renderScale);
    height = unsigned(recommendedHeight * vrConfig.renderScale);
    char text[24];
    snprintf(text, sizeof(text), "%u", recommendedWidth);
    WritePrivateProfileStringA("GuruminModern", "VREyeWidth", text, modernIni);
    snprintf(text, sizeof(text), "%u", recommendedHeight);
    WritePrivateProfileStringA("GuruminModern", "VREyeHeight", text, modernIni);
  } else {
    unsigned cachedWidth = GetPrivateProfileIntA("GuruminModern", "VREyeWidth", 0, modernIni);
    unsigned cachedHeight = GetPrivateProfileIntA("GuruminModern", "VREyeHeight", 0, modernIni);
    width = cachedWidth ? unsigned(cachedWidth * vrConfig.renderScale) : 2048;
    height = cachedHeight ? unsigned(cachedHeight * vrConfig.renderScale) : 2048;
    log("VR runtime size not yet available; using %ux%u", width, height);
  }
  float limit = float(vrConfig.maxEye);
  float fit = std::min(1.f, std::min(limit / width, limit / height));
  vrEyeWidth = std::max(512u, unsigned(width * fit) & ~1u);
  vrEyeHeight = std::max(512u, unsigned(height * fit) & ~1u);
  // The side-by-side pass is the game's output; the HUD keeps its 4:3 canvas.
  modernSettings.resolution = {int(vrEyeWidth * 2), int(vrEyeHeight)};
  unsigned canvas = std::min(vrEyeWidth * 2, vrEyeHeight * 4 / 3);
  vrHudWidth = std::min(canvas, 1600u) & ~1u;
  vrHudHeight = (vrHudWidth * 3 / 4) & ~1u;
  log("VR session eye=%ux%u pass=%ux%u hud=%ux%u scale=%s singlePass=%d recommended=%ux%u",
      vrEyeWidth, vrEyeHeight, vrEyeWidth * 2, vrEyeHeight, vrHudWidth, vrHudHeight,
      vrConfig.rig.diorama ? "diorama" : "life", vrConfig.singlePass,
      recommendedWidth, recommendedHeight);
}

static void vrReleaseImages() {
  auto release = [](auto *&p) {
    if (p)
      p->Release();
    p = nullptr;
  };
  for (auto &row : vrImages.surface)
    for (auto &s : row)
      release(s);
  for (auto &row : vrImages.texture)
    for (auto &t : row)
      release(t);
  for (auto &f : vrImages.fence)
    release(f);
  release(vrImages.flush);
  release(vrImages.readbackEye);
  release(vrImages.readbackHud);
  release(vrImages.overlaySurface);
  release(vrImages.overlay);
  release(vrSceneSurface);
  release(vrSavedTarget);
  vrImages.ready = vrImages.shared = false;
  vrImages.pending[0] = vrImages.pending[1] = false;
}
static void vrDeviceLost() {
  if (!vrSession)
    return;
  if (vrChannel.shared) {
    gurumin::vr::Lock lock(vrChannel, 50);
    if (lock) {
      vrChannel.shared->images = {};
      vrChannel.shared->game.rendering = 0;
      if (vrFrameSlot >= 0 &&
          vrChannel.shared->slot[vrFrameSlot].state == gurumin::vr::SlotState::Rendering)
        vrChannel.shared->slot[vrFrameSlot].state = gurumin::vr::SlotState::Empty;
    }
  }
  vrFrameSlot = -1;
  vrPassRan = vrHudRedirected = false;
  vrReleaseImages();
}

static void vrInstallHooks();
static void vrDeviceCreated(IDirect3DDevice9 *d) {
  if (!vrSession || !d)
    return;
  vrInstallHooks();
  vrReleaseImages();
  using namespace gurumin::vr;
  if (outputWidth != vrEyeWidth * 2 || outputHeight != vrEyeHeight) {
    log("VR disabled: device output %ux%u differs from pass %ux%u", outputWidth,
        outputHeight, vrEyeWidth * 2, vrEyeHeight);
    return;
  }
  uint32_t handles[2][ImageCount]{};
  auto create = [&](bool share) {
    for (unsigned slot = 0; slot < 2; ++slot)
      for (unsigned i = 0; i < ImageCount; ++i) {
        UINT w = i == HudImage ? vrHudWidth : vrEyeWidth;
        UINT h = i == HudImage ? vrHudHeight : vrEyeHeight;
        HANDLE handle = nullptr;
        if (FAILED(d->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8,
                                    D3DPOOL_DEFAULT, &vrImages.texture[slot][i],
                                    share ? &handle : nullptr)) ||
            (share && (!handle || handle == INVALID_HANDLE_VALUE)) ||
            FAILED(vrImages.texture[slot][i]->GetSurfaceLevel(0, &vrImages.surface[slot][i])))
          return false;
        handles[slot][i] = uint32_t(uintptr_t(handle));
      }
    return true;
  };
  vrImages.shared = vrConfig.zeroCopy && create(true);
  if (!vrImages.shared) {
    vrReleaseImages();
    if (!create(false)) {
      log("VR eye image allocation failed; VR disabled");
      vrReleaseImages();
      return;
    }
  }
  bool ok = true;
  for (auto &fence : vrImages.fence)
    ok = ok && SUCCEEDED(d->CreateQuery(D3DQUERYTYPE_EVENT, &fence));
  ok = ok && SUCCEEDED(d->CreateTexture(outputWidth, outputHeight, 1, D3DUSAGE_RENDERTARGET,
                                        D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT,
                                        &vrImages.overlay, nullptr)) &&
       SUCCEEDED(vrImages.overlay->GetSurfaceLevel(0, &vrImages.overlaySurface));
  if (!ok) {
    log("VR overlay allocation failed; VR disabled");
    vrReleaseImages();
    return;
  }
  vrImages.generation = ++vrGenerations;
  vrImages.ready = true;
  if (vrChannel.shared) {
    gurumin::vr::Lock lock(vrChannel, 100);
    if (lock) {
      auto &images = vrChannel.shared->images;
      images = {};
      if (vrImages.shared) {
        images.generation = vrImages.generation;
        images.width = vrEyeWidth;
        images.height = vrEyeHeight;
        images.hudWidth = vrHudWidth;
        images.hudHeight = vrHudHeight;
        std::memcpy(images.handle, handles, sizeof(handles));
      }
    }
  }
  log("VR images generation=%u shared=%d eye=%ux%u hud=%ux%u", vrImages.generation,
      vrImages.shared, vrEyeWidth, vrEyeHeight, vrHudWidth, vrHudHeight);
}

static void vrPollFences() {
  for (unsigned slot = 0; slot < 2; ++slot) {
    if (!vrImages.pending[slot] || !vrImages.fence[slot])
      continue;
    BOOL done = FALSE;
    HRESULT result = vrImages.fence[slot]->GetData(&done, sizeof(done), 0);
    if (result == S_FALSE)
      continue;
    gurumin::vr::Lock lock(vrChannel, 5);
    if (!lock)
      continue;
    vrImages.pending[slot] = false;
    auto &target = vrChannel.shared->slot[slot];
    if (result == S_OK && target.state == gurumin::vr::SlotState::Ready &&
        target.request.sequence == vrImages.pendingSequence[slot])
      target.gpuReady = 1;
  }
}

static bool vrClaim() {
  using namespace gurumin::vr;
  if (!vrImages.ready || !vrHostConnected())
    return false;
  Lock lock(vrChannel, 2);
  if (!lock)
    return false;
  ULONGLONG now = GetTickCount64();
  for (int i = 0; i < 2; ++i) {
    auto &slot = vrChannel.shared->slot[i];
    const auto &r = slot.request;
    if (slot.state == SlotState::Requested && !slot.cancelled &&
        r.width == vrEyeWidth && r.height == vrEyeHeight && r.input.focused &&
        r.input.headValid && now >= r.input.tickMs && now - r.input.tickMs < 200) {
      slot.state = SlotState::Rendering;
      vrFrameRequest = r;
      vrLastRequest = r;
      vrHaveRequest = true;
      vrFrameSlot = i;
      return true;
    }
  }
  return false;
}

static bool vrSceneMatrix(const float *m) {
  return m && (m == reinterpret_cast<const float *>(gameBase + vrconst::MainVP) ||
               m == reinterpret_cast<const float *>(gameBase + vrconst::AltVP) ||
               !memcmp(m, reinterpret_cast<void *>(gameBase + vrconst::MainVP), 64) ||
               !memcmp(m, reinterpret_cast<void *>(gameBase + vrconst::AltVP), 64));
}
static bool vrAlternateMatrix(const float *m) {
  auto main = reinterpret_cast<const void *>(gameBase + vrconst::MainVP);
  auto alt = reinterpret_cast<const void *>(gameBase + vrconst::AltVP);
  return m && !memcmp(m, alt, 64) && memcmp(m, main, 64);
}
static const float *vrRegionMatrix(int region, const float *nativeMatrix) {
  if (region < 0 || region >= vrRegionCount)
    return nativeMatrix;
  return vrAlternateMatrix(nativeMatrix) ? vrRegions[region].vpAlt : vrRegions[region].vp;
}

template <class F> static unsigned vrEachRegion(F draw) {
  auto d = game<IDirect3DDevice9 *>(vrconst::Device);
  if (!d || !setViewport || !vrRegionCount)
    return draw();
  D3DVIEWPORT9 native = actualViewport;
  DWORD scissorEnabled = FALSE;
  RECT scissor{};
  d->GetRenderState(D3DRS_SCISSORTESTENABLE, &scissorEnabled);
  d->GetScissorRect(&scissor);
  d->SetRenderState(D3DRS_SCISSORTESTENABLE, TRUE);
  unsigned result = 0;
  for (int region = 0; region < vrRegionCount; ++region) {
    const auto &v = vrRegions[region].viewport;
    vrRegion = region;
    setViewport(d, &v);
    RECT rect{LONG(v.X), LONG(v.Y), LONG(v.X + v.Width), LONG(v.Y + v.Height)};
    d->SetScissorRect(&rect);
    result = draw();
  }
  vrRegion = -1;
  setViewport(d, &native);
  d->SetScissorRect(&scissor);
  d->SetRenderState(D3DRS_SCISSORTESTENABLE, scissorEnabled);
  return result;
}

static gurumin::vr::Vec3 vrGameVector(uintptr_t rva) {
  auto p = reinterpret_cast<const float *>(gameBase + rva);
  return {p[0], p[1], p[2]};
}
static void vrOrientFromNativeCamera() {
  auto eye = vrGameVector(vrconst::CameraEye), target = vrGameVector(vrconst::CameraTarget);
  vrRig.setYawFromForward(target.x - eye.x, target.y - eye.y);
}
static gurumin::vr::Vec3 vrPlayerPosition() {
  using gurumin::vr::Vec3;
  Vec3 position = vrGameVector(vrconst::PlayerPosition);
  unsigned tick = game<unsigned>(vrconst::Tick);
  double now = nowSeconds();
  if (tick != vrPlayerTick) {
    bool consecutive = tick - vrPlayerTick <= 2;
    vrPlayerPrevious = consecutive ? vrPlayerCurrent : position;
    vrPlayerCurrent = position;
    vrPlayerTick = tick;
    vrPlayerStamp = now;
  }
  float alpha = std::clamp(float((now - vrPlayerStamp) * 30.), 0.f, 1.f);
  return vrPlayerPrevious + (vrPlayerCurrent - vrPlayerPrevious) * alpha;
}

// Once per frame: recenter, stick steering and anchor following.
static void vrUpdateRig(const gurumin::vr::Input &input) {
  using namespace gurumin::vr;
  if (vrRigFrame == frames)
    return;
  vrRigFrame = frames;
  double now = nowSeconds();
  float dt = vrLastRigTime ? float(now - vrLastRigTime) : 0.f;
  vrLastRigTime = now;
  vrRig.config = vrConfig.rig;
  if (input.headValid) {
    vrLastHead = input.head;
    vrHeadValid = true;
  }
  int scene = game<int>(vrconst::Scene);
  char map[64]{};
  const char *nativeMap = reinterpret_cast<const char *>(gameBase + vrconst::MapName);
  for (unsigned i = 0; i < sizeof(map) - 1 && nativeMap[i]; ++i)
    map[i] = nativeMap[i];
  if (scene != vrRigScene || memcmp(map, vrRigMap, sizeof(map))) {
    // New area: face the way the authored camera looks, snap the anchor.
    vrRigScene = scene;
    memcpy(vrRigMap, map, sizeof(map));
    vrOrientFromNativeCamera();
    vrAnchor.reset();
  }
  if (!vrRig.oriented)
    vrOrientFromNativeCamera();
  bool thumbs = (input.buttons[0] & Thumb) && (input.buttons[1] & Thumb);
  if (thumbs && !vrThumbsWereDown)
    vrRecenterRequested = true;
  vrThumbsWereDown = thumbs;
  if ((vrRecenterRequested || !vrRig.recentered) && vrHeadValid) {
    vrRig.recenter(vrLastHead);
    vrRig.resetFraming();
    vrOrientFromNativeCamera();
    vrAnchor.reset();
    log("VR recentered yaw=%.3f", vrRig.yaw);
    vrRecenterRequested = false;
  }
  // Right stick: VR controller or physical pad, whichever is pushed further.
  float sx = 0, sy = 0;
  if (input.focused) {
    float x = input.stick[1][0], y = input.stick[1][1];
    if (std::hypot(x, y) > .15f) {
      sx = x;
      sy = y;
    }
  }
  if (std::hypot(vrPadRight[0], vrPadRight[1]) > std::hypot(sx, sy)) {
    sx = vrPadRight[0];
    sy = vrPadRight[1];
  }
  bool scripted = !fixedGameplayOrbit();
  if (!game<int>(vrconst::MenuOpen))
    vrRig.steer(sx, sy, dt);
  // Scripted scenes follow the authored camera target; gameplay the player.
  Vec3 target = scripted ? vrGameVector(vrconst::CameraTarget) : vrPlayerPosition();
  vrRig.anchor = vrAnchor.update(target, dt, vrRig.config);
}

static bool vrComputeRegions(const gurumin::vr::Request &request) {
  auto main = reinterpret_cast<const float *>(gameBase + vrconst::MainProjection);
  auto alt = reinterpret_cast<const float *>(gameBase + vrconst::AltProjection);
  if (main[11] != 1.f || main[15] != 0.f)
    return false;
  bool altValid = alt[11] == 1.f && alt[15] == 0.f && std::isfinite(alt[10]) &&
                  std::isfinite(alt[14]);
  for (int h = 0; h < 2; ++h) {
    auto &region = vrRegions[h];
    region.viewport = {DWORD(h * vrEyeWidth), 0, vrEyeWidth, vrEyeHeight, 0.f, 1.f};
    float projection[16], projectionAlt[16];
    vrRig.eyeView(request.eye[h], region.view);
    if (!gurumin::vr::projection(request.fov[h], main[10], main[14], projection) ||
        !gurumin::vr::projection(request.fov[h], altValid ? alt[10] : main[10],
                                 altValid ? alt[14] : main[14], projectionAlt))
      return false;
    gurumin::vr::multiply(region.view, projection, region.vp);
    gurumin::vr::multiply(region.view, projectionAlt, region.vpAlt);
    for (float value : region.vp)
      if (!std::isfinite(value))
        return false;
    memcpy(vrLastEye[h], region.vp, 64);
    memcpy(vrLastEyeAlt[h], region.vpAlt, 64);
  }
  vrRegionCount = 2;
  vrLastEyesValid = true;
  // Billboard axes follow the eye only if the native view uses our layout.
  float factored[16];
  vrAxesCompatible = factorPerspectiveView(
      reinterpret_cast<const float *>(gameBase + vrconst::MainVP), main, factored);
  if (vrAxesCompatible) {
    auto nativeView = reinterpret_cast<const float *>(gameBase + vrconst::NativeView);
    for (int i = 0; i < 16 && vrAxesCompatible; ++i)
      vrAxesCompatible = std::abs(factored[i] - nativeView[i]) <=
                         .01f * std::max(1.f, std::abs(nativeView[i]));
  }
  return true;
}

// Scene root (RVA 0x1d3360): one traversal for both eyes, or two traversals
// with VRSinglePass=0. Returns false to let the native scene render flat.
static bool vrSceneRoot(void(__cdecl *native)()) {
  ++vrStatRootCalls;
  if (!vrSession || !supported || !vrImages.ready || manualBookActive())
    return false;
  vrPollFences();
  if (vrPassFrame != frames && vrFrameSlot < 0)
    vrClaim();
  if (!vrHaveRequest)
    return false;
  const auto &request = vrFrameSlot >= 0 ? vrFrameRequest : vrLastRequest;
  vrUpdateRig(request.input);
  if (!vrRig.recentered || !vrComputeRegions(request))
    return false;
  // The menu shrinks the flat scene viewport; VR eyes always stay full.
  int shrinkX = game<int>(vrconst::SceneShrinkX), shrinkY = game<int>(vrconst::SceneShrinkY);
  game<int>(vrconst::SceneShrinkX) = 0;
  game<int>(vrconst::SceneShrinkY) = 0;
  unsigned unscoped = vrUnscopedDraws;
  if (vrConfig.singlePass) {
    vrPass = true;
    native();
    vrPass = false;
  } else
    for (int region = 0; region < vrRegionCount; ++region) {
      vrFixedRegion = region;
      vrPass = true;
      native();
      vrPass = false;
      vrFixedRegion = -1;
    }
  game<int>(vrconst::SceneShrinkX) = shrinkX;
  game<int>(vrconst::SceneShrinkY) = shrinkY;
  vrPassRan = true;
  vrPassFrame = frames;
  ++vrStatPasses;
  static unsigned reported;
  if (vrUnscopedDraws != unscoped && reported++ < 10)
    log("VR pass frame=%u drew %u scene draws outside eye regions", frames,
        vrUnscopedDraws - unscoped);
  static bool first;
  if (!first) {
    first = true;
    log("VR first stereo pass: singlePass=%d axesCompatible=%d slot=%d", vrConfig.singlePass,
        vrAxesCompatible, vrFrameSlot);
  }
  return true;
}

// The native composite blit (ret 0x3a7e21): copy the eye halves and send all
// following HUD draws into the overlay target with coverage alpha.
static bool vrBlit(IDirect3DDevice9 *d, D3DPRIMITIVETYPE type, UINT count, const void *data,
                   UINT stride) {
  static unsigned rejected;
  ++vrStatBlits;
  if (!vrPassRan || vrPassFrame != frames || vrHudRedirected || !vrImages.overlaySurface) {
    if (diagnostics && vrPassRan && rejected++ < 8)
      log("VR blit skipped pass=%d passFrame=%u frame=%u redirected=%d", vrPassRan,
          vrPassFrame, frames, vrHudRedirected);
    return false;
  }
  IDirect3DBaseTexture9 *base = nullptr;
  if (FAILED(d->GetTexture(0, &base)) || !base)
    return false;
  // Only the verified scene texture of the current parity (as for FXAA).
  unsigned parity = game<unsigned>(0x1de8d4c);
  bool texture = base->GetType() == D3DRTYPE_TEXTURE && parity <= 1 &&
                 base == game<IDirect3DBaseTexture9 *>(0x1da35d8 + parity * 4);
  if (!texture && rejected++ < 8)
    log("VR blit texture %p parity=%u expected %p/%p", static_cast<void *>(base), parity,
        static_cast<void *>(game<IDirect3DBaseTexture9 *>(0x1da35d8)),
        static_cast<void *>(game<IDirect3DBaseTexture9 *>(0x1da35dc)));
  if (texture && vrSceneSurface) {
    vrSceneSurface->Release();
    vrSceneSurface = nullptr;
  }
  if (texture)
    static_cast<IDirect3DTexture9 *>(base)->GetSurfaceLevel(0, &vrSceneSurface);
  base->Release();
  if (!vrSceneSurface)
    return false;
  if (vrSavedTarget) {
    vrSavedTarget->Release();
    vrSavedTarget = nullptr;
  }
  if (FAILED(d->GetRenderTarget(0, &vrSavedTarget)) ||
      FAILED(d->SetRenderTarget(0, vrImages.overlaySurface)))
    return false;
  d->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.f, 0);
  // The HUD depth-tests against what the native blit leaves behind. Replay
  // the blit with colour writes off so HUD visibility matches the flat game.
  DWORD colorWrites = 0xf;
  d->GetRenderState(D3DRS_COLORWRITEENABLE, &colorWrites);
  d->SetRenderState(D3DRS_COLORWRITEENABLE, 0);
  drawUP(d, type, count, data, stride);
  d->SetRenderState(D3DRS_COLORWRITEENABLE, colorWrites);
  // Premultiplied colour plus coverage alpha regardless of native blending.
  d->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, TRUE);
  d->SetRenderState(D3DRS_SRCBLENDALPHA, D3DBLEND_ONE);
  d->SetRenderState(D3DRS_DESTBLENDALPHA, D3DBLEND_INVSRCALPHA);
  vrHudRedirected = true;
  ++vrStatRedirects;
  return true;
}

// A native fullscreen fade or iris drawn during the HUD: apply it to both
// eyes on the stereo scene target (centred per eye, clipped to its half),
// then let it darken the HUD drawn so far without adding panel coverage, so
// the world behind the panel is not darkened twice.
static HRESULT vrFadeEyes(IDirect3DDevice9 *d, D3DPRIMITIVETYPE type, UINT count,
                          const void *vertices, UINT stride) {
  IDirect3DSurface9 *current = nullptr;
  if (vrSceneSurface && SUCCEEDED(d->GetRenderTarget(0, &current)) && current) {
    D3DVIEWPORT9 viewport{};
    RECT scissor{};
    DWORD scissorEnabled = FALSE, separateAlpha = FALSE;
    d->GetViewport(&viewport);
    d->GetScissorRect(&scissor);
    d->GetRenderState(D3DRS_SCISSORTESTENABLE, &scissorEnabled);
    d->GetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, &separateAlpha);
    if (SUCCEEDED(setTarget(d, 0, vrSceneSurface))) {
      D3DVIEWPORT9 pass{0, 0, outputWidth, outputHeight, 0.f, 1.f};
      setViewport(d, &pass);
      d->SetRenderState(D3DRS_SCISSORTESTENABLE, TRUE);
      d->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
      std::vector<unsigned char> copy(size_t(count + 2) * stride);
      for (int region = 0; region < vrRegionCount; ++region) {
        const auto &v = vrRegions[region].viewport;
        memcpy(copy.data(), vertices, copy.size());
        float shift = float(v.X) + v.Width * .5f - outputWidth * .5f;
        for (UINT i = 0; i < count + 2; ++i)
          reinterpret_cast<float *>(copy.data() + size_t(i) * stride)[0] += shift;
        RECT rect{LONG(v.X), LONG(v.Y), LONG(v.X + v.Width), LONG(v.Y + v.Height)};
        d->SetScissorRect(&rect);
        drawUP(d, type, count, copy.data(), stride);
      }
      setTarget(d, 0, current);
      setViewport(d, &viewport);
      d->SetScissorRect(&scissor);
      d->SetRenderState(D3DRS_SCISSORTESTENABLE, scissorEnabled);
      d->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, separateAlpha);
    }
  }
  if (current)
    current->Release();
  DWORD sourceAlpha = D3DBLEND_ONE, destinationAlpha = D3DBLEND_INVSRCALPHA;
  d->GetRenderState(D3DRS_SRCBLENDALPHA, &sourceAlpha);
  d->GetRenderState(D3DRS_DESTBLENDALPHA, &destinationAlpha);
  d->SetRenderState(D3DRS_SRCBLENDALPHA, D3DBLEND_ZERO);
  d->SetRenderState(D3DRS_DESTBLENDALPHA, D3DBLEND_ONE);
  HRESULT result = submitUP(d, type, count, vertices, stride);
  d->SetRenderState(D3DRS_SRCBLENDALPHA, sourceAlpha);
  d->SetRenderState(D3DRS_DESTBLENDALPHA, destinationAlpha);
  return result;
}

static IDirect3DSurface9 *vrRedirectTarget(DWORD index, IDirect3DSurface9 *target) {
  if (vrHudRedirected && index == 0 && target && target == vrSavedTarget &&
      vrImages.overlaySurface)
    return vrImages.overlaySurface;
  return target;
}

// Desktop mirror: side-by-side eyes with the HUD composited on top.
static void vrMirror(IDirect3DDevice9 *d) {
  IDirect3DSurface9 *backbuffer = nullptr;
  if (!vrSceneSurface || FAILED(d->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backbuffer)))
    return;
  RECT pass{0, 0, LONG(outputWidth), LONG(outputHeight)};
  d->StretchRect(vrSceneSurface, &pass, backbuffer, nullptr, D3DTEXF_POINT);
  backbuffer->Release();
  IDirect3DStateBlock9 *saved = nullptr;
  if (FAILED(d->CreateStateBlock(D3DSBT_ALL, &saved)))
    return;
  saved->Capture();
  d->SetVertexShader(nullptr);
  d->SetPixelShader(nullptr);
  d->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);
  d->SetTexture(0, vrImages.overlay);
  d->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
  d->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
  d->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
  d->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
  d->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
  d->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
  d->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
  d->SetRenderState(D3DRS_ZENABLE, FALSE);
  d->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
  d->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
  d->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
  d->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
  d->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
  d->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
  d->SetRenderState(D3DRS_FOGENABLE, FALSE);
  d->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
  struct Vertex {
    float x, y, z, w, u, v;
  };
  float w = float(outputWidth) - .5f, h = float(outputHeight) - .5f;
  Vertex quad[] = {{-.5f, -.5f, 0, 1, 0, 0}, {w, -.5f, 0, 1, 1, 0},
                   {-.5f, h, 0, 1, 0, 1}, {w, h, 0, 1, 1, 1}};
  drawUP(d, D3DPT_TRIANGLESTRIP, 2, quad, sizeof(Vertex));
  saved->Apply();
  saved->Release();
}

static bool vrCopyPixels(IDirect3DDevice9 *d, IDirect3DSurface9 *source,
                         IDirect3DSurface9 *readback, uint8_t *destination,
                         unsigned width, unsigned height) {
  D3DLOCKED_RECT locked{};
  if (!destination || FAILED(d->GetRenderTargetData(source, readback)) ||
      FAILED(readback->LockRect(&locked, nullptr, D3DLOCK_READONLY)))
    return false;
  bool ok = locked.pBits && locked.Pitch >= int(width * 4);
  for (unsigned row = 0; ok && row < height; ++row)
    memcpy(destination + size_t(row) * width * 4,
           static_cast<const uint8_t *>(locked.pBits) + size_t(row) * locked.Pitch, width * 4);
  readback->UnlockRect();
  return ok;
}

// CPU fallback (native Windows D3D9 cannot export shared handles).
static bool vrPixelPath(IDirect3DDevice9 *d, unsigned slot, bool world) {
  using namespace gurumin::vr;
  if (!vrPixels.view[0]) {
    PixelSections sections{vrImages.generation, vrEyeWidth, vrEyeHeight, vrHudWidth,
                           vrHudHeight, 0};
    if (!vrPixels.open(vrChannel.token, sections, true)) {
      static bool reported;
      if (!reported) {
        reported = true;
        log("VR pixel sections unavailable");
      }
      return false;
    }
    Lock lock(vrChannel, 50);
    if (!lock)
      return false;
    vrChannel.shared->pixels = sections;
    log("VR CPU image transfer enabled generation=%u", sections.generation);
  }
  if (!vrImages.readbackEye &&
      FAILED(d->CreateOffscreenPlainSurface(vrEyeWidth, vrEyeHeight, D3DFMT_A8R8G8B8,
                                            D3DPOOL_SYSTEMMEM, &vrImages.readbackEye, nullptr)))
    return false;
  if (!vrImages.readbackHud &&
      FAILED(d->CreateOffscreenPlainSurface(vrHudWidth, vrHudHeight, D3DFMT_A8R8G8B8,
                                            D3DPOOL_SYSTEMMEM, &vrImages.readbackHud, nullptr)))
    return false;
  bool ok = true;
  if (world)
    for (unsigned h = 0; h < 2 && ok; ++h)
      ok = vrCopyPixels(d, vrImages.surface[slot][h], vrImages.readbackEye,
                        vrPixels.view[pixelBuffer(slot, h)], vrEyeWidth, vrEyeHeight);
  return ok && vrCopyPixels(d, vrImages.surface[slot][HudImage], vrImages.readbackHud,
                            vrPixels.view[pixelBuffer(slot, HudImage)], vrHudWidth,
                            vrHudHeight);
}

static void vrPublish(IDirect3DDevice9 *d, bool world) {
  using namespace gurumin::vr;
  int slot = vrFrameSlot;
  vrFrameSlot = -1;
  // HUD / flat screen: the proportional 4:3 canvas in the middle of the pass.
  unsigned canvas = std::min(outputWidth, outputHeight * 4 / 3);
  RECT crop{LONG((outputWidth - canvas) / 2), 0, LONG((outputWidth + canvas) / 2),
            LONG(outputHeight)};
  // Eyes are copied after the HUD so native fades drawn onto them count.
  if (world && vrSceneSurface)
    for (unsigned h = 0; h < 2; ++h) {
      RECT eye{LONG(h * vrEyeWidth), 0, LONG((h + 1) * vrEyeWidth), LONG(vrEyeHeight)};
      d->StretchRect(vrSceneSurface, &eye, vrImages.surface[slot][h], nullptr, D3DTEXF_POINT);
    }
  IDirect3DSurface9 *source = nullptr;
  if (world)
    (source = vrImages.overlaySurface)->AddRef();
  else
    d->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &source);
  bool ok = source && SUCCEEDED(d->StretchRect(source, &crop,
                                              vrImages.surface[slot][HudImage], nullptr,
                                              D3DTEXF_LINEAR));
  // Debug probe ("GuruminVR\\probe" exists): save the published source and
  // its HUD/screen copy every 300 frames.
  static int probing = -1;
  if (probing < 0)
    probing = GetFileAttributesA("GuruminVR\\probe") != INVALID_FILE_ATTRIBUTES;
  if (probing && frames % 300 == 0 && source) {
    using SaveFn = HRESULT(WINAPI *)(const char *, int, IDirect3DSurface9 *, const void *,
                                     const RECT *);
    if (HMODULE d3dx = GetModuleHandleA("d3dx9_43.dll"))
      if (auto save = reinterpret_cast<SaveFn>(reinterpret_cast<void *>(GetProcAddress(d3dx, "D3DXSaveSurfaceToFileA")))) {
        HRESULT a = save("GuruminVR\\probe-game-source.bmp", 0, source, nullptr, nullptr);
        HRESULT b = save("GuruminVR\\probe-game-copy.bmp", 0,
                         vrImages.surface[slot][HudImage], nullptr, nullptr);
        log("VR probe frame=%u world=%d copy=%d save=%08lx/%08lx", frames, int(world), int(ok),
            (unsigned long)a, (unsigned long)b);
      }
  }
  if (source)
    source->Release();
  bool zero = false;
  {
    Lock lock(vrChannel, 5);
    zero = lock && vrImages.shared &&
           vrChannel.shared->images.opened == vrImages.generation;
  }
  // Until the host has opened the shared images, fall back to CPU transfer
  // only when sharing is impossible or the host has had time to try.
  bool pixel = !zero && ok &&
               (!vrImages.shared ||
                (vrHostSeenTick && GetTickCount64() - vrHostSeenTick > 3000));
  if (pixel)
    ok = vrPixelPath(d, unsigned(slot), world);
  if (ok && zero) {
    vrImages.fence[slot]->Issue(D3DISSUE_END);
    BOOL done = FALSE;
    vrImages.fence[slot]->GetData(&done, sizeof(done), D3DGETDATA_FLUSH);
  }
  ok = ok && (zero || pixel);
  Lock lock(vrChannel, 50);
  if (!lock) {
    vrOrphanSlot = slot;
    return;
  }
  auto &target = vrChannel.shared->slot[slot];
  if (target.state != SlotState::Rendering ||
      target.request.sequence != vrFrameRequest.sequence)
    return;
  if (!ok || target.cancelled) {
    target.state = SlotState::Empty;
    target.cancelled = 0;
    return;
  }
  target.content = world ? (World | Hud) : Screen;
  ++(world ? vrStatWorld : vrStatScreen);
  target.sharedImage = zero ? vrImages.generation : 0;
  target.gpuReady = zero ? 0 : 1;
  target.pixelGeneration = zero ? 0 : vrPixels.generation;
  target.state = SlotState::Ready;
  vrImages.pending[slot] = zero;
  vrImages.pendingSequence[slot] = vrFrameRequest.sequence;
}

static void vrPublishGameState() {
  if (!vrChannel.shared)
    return;
  gurumin::vr::Lock lock(vrChannel, 1);
  if (!lock)
    return;
  auto &g = vrChannel.shared->game;
  g.tickMs = GetTickCount64();
  g.rendering = vrImages.ready;
  g.eyeWidth = vrEyeWidth;
  g.eyeHeight = vrEyeHeight;
  g.hudWidth = vrHudWidth;
  g.hudHeight = vrHudHeight;
  g.scaleMode = vrConfig.rig.diorama;
  g.hudDistance = vrConfig.hudDistance;
  g.hudWidthMeters = vrConfig.hudWidth;
  g.screenDistance = vrConfig.screenDistance;
  g.screenWidthMeters = vrConfig.screenWidth;
  if (vrOrphanSlot >= 0) {
    auto &slot = vrChannel.shared->slot[vrOrphanSlot];
    if (slot.state == gurumin::vr::SlotState::Rendering)
      slot.state = gurumin::vr::SlotState::Empty;
    vrOrphanSlot = -1;
  }
}

static void vrBeforePresent(IDirect3DDevice9 *d) {
  if (!vrSession || !supported)
    return;
  vrPollFences();
  if (vrHostConnected()) {
    if (!vrHostSeenTick)
      vrHostSeenTick = GetTickCount64();
    // A pad absent at startup disables native pad polling; VR controllers
    // still need the poll to reach the game.
    game<int>(vrconst::UseGamepad) = 1;
  }
  if (vrHudRedirected) {
    if (vrSavedTarget)
      d->SetRenderTarget(0, vrSavedTarget);
    d->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
  }
  // presentHook has already advanced `frames`; these flags are per frame.
  bool world = vrPassRan && vrHudRedirected;
  if (!world && vrFrameSlot < 0 && vrImages.ready)
    vrClaim();
  if (vrFrameSlot >= 0 && vrImages.ready)
    vrPublish(d, world);
  if (world && vrConfig.desktopMirror)
    vrMirror(d);
  if (vrSceneSurface) {
    vrSceneSurface->Release();
    vrSceneSurface = nullptr;
  }
  if (vrSavedTarget) {
    vrSavedTarget->Release();
    vrSavedTarget = nullptr;
  }
  vrHudRedirected = vrPassRan = false;
  vrPublishGameState();
  static unsigned statLines;
  if ((diagnostics || statLines < 20) && frames % 600 == 0 && ++statLines)
    log("VR stats frame=%u rootCalls=%u passes=%u blits=%u redirects=%u world=%u screen=%u "
        "unscoped=%u", frames, vrStatRootCalls, vrStatPasses, vrStatBlits, vrStatRedirects,
        vrStatWorld, vrStatScreen, vrUnscopedDraws);
}

// While the headset shows the game, the desktop window is optional: a
// compositor throttles hidden or covered windows, and a blocking Present
// would then slow the whole game. Skip it and only submit the frame's work.
static bool vrSkipPresent(IDirect3DDevice9 *d) {
  if (!vrSession || !supported || vrConfig.desktopMirror || !vrImages.ready ||
      !vrHostConnected())
    return false;
  if (!vrImages.flush && FAILED(d->CreateQuery(D3DQUERYTYPE_EVENT, &vrImages.flush)))
    return false;
  vrImages.flush->Issue(D3DISSUE_END);
  BOOL done = FALSE;
  vrImages.flush->GetData(&done, sizeof(done), D3DGETDATA_FLUSH);
  return true;
}

// Pace the game to the headset: wait for the host's next request.
static void vrAfterPresent() {
  if (!vrSession || !supported)
    return;
  vrSuperviseHost();
  if (!vrHostConnected() || !vrImages.ready) {
    double deadline = vrFallbackPacer.next(nowSeconds(), 90);
    while (nowSeconds() < deadline - .002)
      Sleep(1);
    return;
  }
  double limit = nowSeconds() + .025;
  for (;;) {
    vrPollFences();
    auto *slots = vrChannel.shared->slot;
    if (slots[0].state == gurumin::vr::SlotState::Requested ||
        slots[1].state == gurumin::vr::SlotState::Requested || !vrHostConnected() ||
        nowSeconds() >= limit)
      break;
    Sleep(0);
    YieldProcessor();
  }
}

// Native input poll (RVA 0x3ae330) DIJOYSTATE: overlay VR controllers as an
// Xbox pad, detect the both-sticks recenter chord, claim the right stick.
static void vrInputOverlay(int *state) {
  using namespace gurumin::vr;
  if (!vrSession || !state || !vrChannel.shared)
    return;
  Input input{};
  bool fresh = false;
  {
    Lock lock(vrChannel, 0);
    if (lock) {
      input = vrChannel.shared->latest;
      ULONGLONG now = GetTickCount64();
      fresh = vrHostConnected() && input.focused && now >= input.tickMs &&
              now - input.tickMs < 200;
    }
  }
  auto *bytes = reinterpret_cast<unsigned char *>(state) + 0x30;
  // Without a native pad nothing refreshes the array: start from neutral.
  if (!game<int>(vrconst::XInputModule) && !game<void *>(vrconst::DInputDevice)) {
    for (int i = 0; i < 6; ++i)
      state[i] = 0x8000;
    memset(bytes, 0, 12);
  }
  float px = (state[3] - 0x8000) / 32767.f, py = -(state[4] - 0x8000) / 32767.f;
  float length = std::hypot(px, py);
  if (length > .25f && std::isfinite(length)) {
    vrPadRight[0] = std::clamp(px, -1.f, 1.f);
    vrPadRight[1] = std::clamp(py, -1.f, 1.f);
  } else
    vrPadRight[0] = vrPadRight[1] = 0;
  auto axis = [](float value) {
    return int(std::clamp(0x8000 + std::lround(value * 32767.f), 0L, 0xffffL));
  };
  if (fresh) {
    float lx = input.stick[0][0], ly = input.stick[0][1];
    float native = std::hypot((state[0] - 0x8000) / 32767.f, (state[1] - 0x8000) / 32767.f);
    if (std::hypot(lx, ly) > std::max(.15f, native)) {
      state[0] = axis(lx);
      state[1] = axis(-ly);
    }
    float lt = input.trigger[0], rt = input.trigger[1];
    if (lt > .05f || rt > .05f)
      state[2] = int(std::clamp(0x8000L + std::lround((lt - rt) * 255.f) * 0x80L, 0L, 0xffffL));
    auto press = [&](int index, bool down) {
      if (down)
        bytes[index] = 0x80;
    };
    press(0, input.buttons[1] & Primary);
    press(1, input.buttons[1] & Secondary);
    press(2, input.buttons[0] & Primary);
    press(3, input.buttons[0] & Secondary);
    press(4, input.squeeze[0] > .6f);
    press(5, input.squeeze[1] > .6f);
    press(6, input.buttons[1] & Menu);
    press(7, input.buttons[0] & Menu);
    press(8, input.buttons[0] & Thumb);
    press(9, input.buttons[1] & Thumb);
    press(10, lt > .5f);
    press(11, rt > .5f);
    vrLastInputTick = GetTickCount64();
  }
  bool left = bytes[8] & 0x80, right = bytes[9] & 0x80;
  if (left && right && !vrChord) {
    vrChord = true;
    vrRecenterRequested = true;
  }
  if (vrChord) {
    // The chord belongs to recentering until both sticks are released.
    bytes[8] = bytes[9] = 0;
    if (!left && !right)
      vrChord = false;
  }
  if (vrHostConnected() && vrImages.ready) {
    state[3] = 0x8000;
    state[4] = 0x8000;
  }
}

// Native movement converter (RVA 0x32cb60 from 0x21d499): left stick moves
// Parin relative to where the head looks.
static bool vrMovement(uintptr_t caller, float stickX, float stickY, float *outX,
                       float *outY) {
  if (!vrSession || caller != 0x21d499 || !vrHeadValid || !vrRig.recentered ||
      !vrHostConnected() || (stickX == 0 && stickY == 0) ||
      game<int>(vrconst::MenuOpen) || !cameraMovementOutputsIndependent(outX, outY))
    return false;
  float gx, gy;
  if (!vrRig.gaze(vrLastHead, gx, gy))
    return false;
  float eye[3] = {0, 0, 0}, target[3] = {gx, gy, 0}, x, y;
  if (!gurumin::cameraRelativeMovement(eye, target, stickX, stickY, x, y))
    return false;
  *outX = x;
  *outY = y;
  return true;
}

// View culling (RVA 0x375320): test scene objects against the eyes.
using VrCullFn = int(__cdecl *)(const float *, const float *, float);
static VrCullFn vrNativeCull;
static int __cdecl vrCullHook(const float *center, const float *vp, float radius) {
  if (!vrSession || !vrLastEyesValid || !vrSceneMatrix(vp))
    return vrNativeCull(center, vp, radius);
  bool alt = vrAlternateMatrix(vp);
  int region = vrPass ? vrCurrentRegion() : -1;
  if (region >= 0)
    return vrNativeCull(center, alt ? vrRegions[region].vpAlt : vrRegions[region].vp, radius);
  for (int eye = 0; eye < 2; ++eye)
    if (vrNativeCull(center, alt ? vrLastEyeAlt[eye] : vrLastEye[eye], radius))
      return 1;
  // Outside the eye pass (per-tick actor updates) keep native visibility too.
  return vrPass ? 0 : vrNativeCull(center, vp, radius);
}

// Zone visibility follows the native look-at point. Draw-only gates are
// opened in VR so every map zone renders; the zone table itself (also read by
// collision) is untouched.
struct VrPatch {
  uintptr_t rva;
  unsigned char original[6], replacement[6];
  unsigned length;
};
// Byte-verified code patches: all or nothing per group.
template <size_t N> static bool vrApplyPatches(const VrPatch (&patches)[N], const char *name) {
  for (const auto &p : patches)
    if (memcmp(reinterpret_cast<void *>(gameBase + p.rva), p.original, p.length)) {
      log("VR %s patch RVA=%lx unexpected; left native", name, (unsigned long)p.rva);
      return false;
    }
  for (const auto &p : patches) {
    auto target = reinterpret_cast<void *>(gameBase + p.rva);
    DWORD old;
    if (!VirtualProtect(target, p.length, PAGE_EXECUTE_READWRITE, &old))
      continue;
    memcpy(target, p.replacement, p.length);
    DWORD unused;
    VirtualProtect(target, p.length, old, &unused);
    FlushInstructionCache(GetCurrentProcess(), target, p.length);
  }
  return true;
}

static void vrPatchZones() {
  static const VrPatch patches[] = {
      {0x350ae6, {0x74, 0x9e}, {0x90, 0x90}, 2},
      {0x35170e, {0x74, 0x9b}, {0x90, 0x90}, 2},
      {0x351f4b, {0x74, 0xc8}, {0x90, 0x90}, 2},
      {0x352946, {0x75, 0x14}, {0xeb, 0x14}, 2},
      {0x1dcf3f, {0x0f, 0x84, 0xf0, 0x01, 0x00, 0x00}, {0x90, 0x90, 0x90, 0x90, 0x90, 0x90}, 6},
  };
  if (vrApplyPatches(patches, "zone gate"))
    log("VR zone draw gates opened");
}

// Without window focus the main loop (0x7a2080) sleeps and the frame
// function (0x60c750) skips its update while the active flag 0x909350 is
// clear. In VR the headset is the display, so always run full frames.
static void vrPatchBackground() {
  static const VrPatch patches[] = {
      {0x3a20c0, {0x75, 0x0f}, {0xeb, 0x0f}, 2},
      {0x20c76a, {0x75, 0x13}, {0xeb, 0x13}, 2},
  };
  if (vrApplyPatches(patches, "background"))
    log("VR keeps running without window focus");
}

static void vrInstallHooks() {
  static bool installed;
  if (installed || !supported || !nativeHooksInstalled)
    return;
  installed = true;
  auto target = reinterpret_cast<void *>(gameBase + 0x375320);
  auto status = MH_CreateHook(target, reinterpret_cast<void *>(vrCullHook),
                              reinterpret_cast<void **>(&vrNativeCull));
  if (status == MH_OK)
    status = MH_EnableHook(target);
  log("VR cull hook status=%d", status);
  if (vrConfig.drawAllZones)
    vrPatchZones();
  vrPatchBackground();
}
