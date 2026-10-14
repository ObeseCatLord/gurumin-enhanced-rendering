// Gurumin VR OpenXR host (x64). Started by the game's d3d9 proxy with
// --channel TOKEN; owns OpenXR presentation, head/eye poses and controller
// sampling. Runs natively on Windows and inside Proton's prefix (wineopenxr).
// Structure adapted from the SS2VR OpenXR host (MIT, same author).
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_D3D11
#define XR_NO_PROTOTYPES
#include "../src/vr_ipc.hpp"
#include "../src/vr_math.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <d3d11.h>
#include <dxgi.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <stdexcept>
#include <string>
#include <vector>
#include <windows.h>

namespace {
using namespace gurumin::vr;
static_assert(sizeof(void *) == 8, "The OpenXR host must be built for x64");
constexpr XrDuration ImageWaitNs = 20'000'000;
constexpr uint64_t CachedHoldMs = 1000;

template <class T> struct Com {
  T *p = nullptr;
  Com() = default;
  Com(const Com &) = delete;
  Com &operator=(const Com &) = delete;
  ~Com() { reset(); }
  T **put() {
    reset();
    return &p;
  }
  T *operator->() const { return p; }
  void reset() {
    if (p)
      p->Release();
    p = nullptr;
  }
};
struct Handle {
  HANDLE h = nullptr;
  ~Handle() {
    if (h && h != INVALID_HANDLE_VALUE)
      CloseHandle(h);
  }
};

std::wstring executableDirectory() {
  std::vector<wchar_t> path(32768);
  DWORD n = GetModuleFileNameW(nullptr, path.data(), DWORD(path.size()));
  if (!n || n >= path.size())
    throw std::runtime_error("Cannot locate host directory");
  std::wstring value(path.data(), n);
  auto slash = value.find_last_of(L"\\/");
  if (slash == std::wstring::npos)
    throw std::runtime_error("Host path has no directory");
  return value.substr(0, slash + 1);
}

struct Log {
  Handle file;
  DWORD written = 0;
  explicit Log(const std::wstring &directory) {
    file.h = CreateFileW((directory + L"gurumin_vr_host.log").c_str(),
                         GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
  }
  void write(const char *message) noexcept {
    if (!file.h || file.h == INVALID_HANDLE_VALUE || written >= 512 * 1024)
      return;
    DWORD n = 0;
    WriteFile(file.h, message, DWORD(std::strlen(message)), &n, nullptr);
    written += n;
    WriteFile(file.h, "\r\n", 2, &n, nullptr);
    written += n;
  }
  template <class... A> void format(const char *f, A... args) noexcept {
    char line[512];
    std::snprintf(line, sizeof(line), f, args...);
    write(line);
  }
};

// Abandoned mutex: the game died mid-write, so its payload is untrusted.
struct Mutex {
  Channel &channel;
  bool held = false;
  Mutex(Channel &c, DWORD wait) : channel(c) {
    DWORD result = WaitForSingleObject(c.mutex, wait);
    if (result == WAIT_ABANDONED) {
      ReleaseMutex(c.mutex);
      throw std::runtime_error("IPC mutex abandoned");
    }
    if (result == WAIT_FAILED)
      throw std::runtime_error("IPC mutex wait failed");
    held = result == WAIT_OBJECT_0;
  }
  ~Mutex() {
    if (held)
      ReleaseMutex(channel.mutex);
  }
  explicit operator bool() const { return held; }
};

#define XR_FUNCTIONS(X)                                                        \
  X(DestroyInstance) X(GetInstanceProperties) X(GetSystem)                     \
  X(GetSystemProperties) X(EnumerateViewConfigurations)                        \
  X(EnumerateViewConfigurationViews) X(EnumerateEnvironmentBlendModes)         \
  X(GetD3D11GraphicsRequirementsKHR) X(CreateSession) X(DestroySession)        \
  X(BeginSession) X(EndSession) X(PollEvent) X(CreateReferenceSpace)          \
  X(DestroySpace) X(LocateSpace) X(EnumerateSwapchainFormats)                  \
  X(CreateSwapchain) X(DestroySwapchain) X(EnumerateSwapchainImages)           \
  X(AcquireSwapchainImage) X(WaitSwapchainImage) X(ReleaseSwapchainImage)      \
  X(WaitFrame) X(BeginFrame) X(EndFrame) X(LocateViews) X(StringToPath)        \
  X(CreateActionSet) X(DestroyActionSet) X(CreateAction)                       \
  X(SuggestInteractionProfileBindings) X(AttachSessionActionSets)              \
  X(SyncActions) X(GetActionStateBoolean) X(GetActionStateFloat)              \
  X(GetActionStateVector2f)

struct Api {
  HMODULE loader = nullptr;
  Log &log;
  bool lossPending = false;
  DXGI_FORMAT colorFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
  PFN_xrGetInstanceProcAddr GetInstanceProcAddr = nullptr;
  PFN_xrEnumerateInstanceExtensionProperties EnumerateInstanceExtensionProperties = nullptr;
  PFN_xrCreateInstance CreateInstance = nullptr;
#define DECLARE(name) PFN_xr##name name = nullptr;
  XR_FUNCTIONS(DECLARE)
#undef DECLARE
  explicit Api(Log &l) : log(l) {}
  void check(XrResult result, const char *operation) {
    if (result == XR_SESSION_LOSS_PENDING)
      lossPending = true;
    if (XR_FAILED(result)) {
      log.format("%s: %d", operation, int(result));
      throw std::runtime_error(operation);
    }
  }
  void cleanup(XrResult result, const char *operation) noexcept {
    if (XR_FAILED(result))
      log.format("%s: %d", operation, int(result));
  }
  template <class T> void load(T &destination, XrInstance instance, const char *name) {
    PFN_xrVoidFunction fn = nullptr;
    check(GetInstanceProcAddr(instance, name, &fn), name);
    if (!fn)
      throw std::runtime_error("Loader returned a null OpenXR function");
    destination = reinterpret_cast<T>(fn);
  }
  void open(const std::wstring &directory) {
    // An absolute path keeps the loader search inside the packaged folder.
    for (const wchar_t *file : {L"openxr_loader.dll", L"libopenxr_loader.dll"}) {
      loader = LoadLibraryExW((directory + file).c_str(), nullptr,
                              LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
                                  LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
      if (loader)
        break;
    }
    if (!loader)
      throw std::runtime_error("openxr_loader.dll missing beside the host");
    FARPROC entry = GetProcAddress(loader, "xrGetInstanceProcAddr");
    static_assert(sizeof(entry) == sizeof(GetInstanceProcAddr));
    std::memcpy(&GetInstanceProcAddr, &entry, sizeof(entry));
    if (!GetInstanceProcAddr)
      throw std::runtime_error("Loader has no xrGetInstanceProcAddr");
    load(EnumerateInstanceExtensionProperties, XR_NULL_HANDLE,
         "xrEnumerateInstanceExtensionProperties");
    load(CreateInstance, XR_NULL_HANDLE, "xrCreateInstance");
  }
  void instanceFunctions(XrInstance instance) {
#define LOAD(name) load(name, instance, "xr" #name);
    XR_FUNCTIONS(LOAD)
#undef LOAD
  }
};
#undef XR_FUNCTIONS

void hresult(HRESULT result, const char *operation, Log &log) {
  if (FAILED(result)) {
    log.format("%s: %08lx", operation, (unsigned long)result);
    throw std::runtime_error(operation);
  }
}
Pose pose(const XrPosef &v) {
  return {{v.orientation.x, v.orientation.y, v.orientation.z, v.orientation.w},
          {v.position.x, v.position.y, v.position.z}};
}
XrPosef xrPose(Pose v) {
  // Runtimes reject slightly denormalized layer orientations.
  Quat q = normalize(v.q);
  if (!finite(v.p))
    return {{0, 0, 0, 1}, {0, 0, 0}};
  return {{q.x, q.y, q.z, q.w}, {v.p.x, v.p.y, v.p.z}};
}

struct ImageOwnership {
  uint32_t index = 0, releasedIndex = 0;
  bool acquired = false, waited = false, released = false;
  bool previousImageAvailable() const {
    return released && !(acquired && index == releasedIndex);
  }
  void didRelease() {
    releasedIndex = index;
    released = true;
    acquired = waited = false;
  }
};

struct Swapchain {
  Api *api = nullptr;
  XrSwapchain handle = XR_NULL_HANDLE;
  uint32_t width = 0, height = 0;
  ImageOwnership ownership;
  std::vector<XrSwapchainImageD3D11KHR> images;
  std::vector<uint8_t> converted;
  ~Swapchain() { destroy(); }
  void destroy() noexcept {
    if (handle) {
      if (ownership.acquired && api && !api->lossPending) {
        try {
          releaseAcquired();
        } catch (...) {
        }
      }
      api->cleanup(api->DestroySwapchain(handle), "xrDestroySwapchain");
    }
    handle = XR_NULL_HANDLE;
    ownership = {};
    images.clear();
    width = height = 0;
  }
  void create(Api &functions, XrSession session, uint32_t w, uint32_t h) {
    api = &functions;
    XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT |
                      XR_SWAPCHAIN_USAGE_SAMPLED_BIT |
                      XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
    info.format = api->colorFormat;
    info.sampleCount = 1;
    info.width = w;
    info.height = h;
    info.faceCount = info.arraySize = info.mipCount = 1;
    api->check(api->CreateSwapchain(session, &info, &handle), "xrCreateSwapchain");
    width = w;
    height = h;
    uint32_t count = 0;
    api->check(api->EnumerateSwapchainImages(handle, 0, &count, nullptr),
               "xrEnumerateSwapchainImages count");
    images.assign(count, XrSwapchainImageD3D11KHR{XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
    api->check(api->EnumerateSwapchainImages(
                   handle, count, &count,
                   reinterpret_cast<XrSwapchainImageBaseHeader *>(images.data())),
               "xrEnumerateSwapchainImages");
    images.resize(count);
    if (images.empty())
      throw std::runtime_error("OpenXR swapchain contains no images");
    for (auto &image : images)
      if (!image.texture)
        throw std::runtime_error("OpenXR returned a null D3D11 texture");
    api->log.format("Swapchain %ux%u format=%d images=%u", w, h,
                    int(info.format), count);
  }
  bool waitImage() {
    if (api->lossPending)
      return false;
    if (!ownership.acquired) {
      XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
      api->check(api->AcquireSwapchainImage(handle, &acquire, &ownership.index),
                 "xrAcquireSwapchainImage");
      ownership.acquired = true;
      if (ownership.index >= images.size())
        throw std::runtime_error("OpenXR swapchain index out of bounds");
    }
    if (!ownership.waited) {
      XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
      wait.timeout = ImageWaitNs;
      XrResult result = api->WaitSwapchainImage(handle, &wait);
      if (result == XR_TIMEOUT_EXPIRED)
        return false;
      api->check(result, "xrWaitSwapchainImage");
      ownership.waited = true;
    }
    return !api->lossPending;
  }
  ID3D11Texture2D *target() const { return images[ownership.index].texture; }
  void upload(ID3D11DeviceContext *context, const uint8_t *bytes) {
    if (api->colorFormat == DXGI_FORMAT_R8G8B8A8_UNORM ||
        api->colorFormat == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB) {
      converted.assign(bytes, bytes + size_t(width) * height * 4);
      for (size_t i = 0; i < converted.size(); i += 4)
        std::swap(converted[i], converted[i + 2]);
      bytes = converted.data();
    }
    context->UpdateSubresource(target(), 0, nullptr, bytes, width * 4, 0);
  }
  void release() {
    if (!ownership.acquired || !ownership.waited)
      throw std::runtime_error("Releasing an unwaited OpenXR image");
    XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    api->check(api->ReleaseSwapchainImage(handle, &release),
               "xrReleaseSwapchainImage");
    ownership.didRelease();
  }
  bool releaseAcquired() {
    if (!ownership.acquired)
      return true;
    if (!waitImage())
      return false;
    release();
    return true;
  }
  XrSwapchainSubImage subimage() const {
    return {handle, {{0, 0}, {int32_t(width), int32_t(height)}}, 0};
  }
};

struct Frame {
  Api &api;
  XrSession session;
  XrTime time;
  bool active = true;
  Frame(Api &a, XrSession s, XrTime t) : api(a), session(s), time(t) {}
  ~Frame() {
    if (active) {
      XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};
      end.displayTime = time;
      end.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
      api.cleanup(api.EndFrame(session, &end), "xrEndFrame during unwind");
    }
  }
  void submit(const std::vector<const XrCompositionLayerBaseHeader *> &layers) {
    XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};
    end.displayTime = time;
    end.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    end.layerCount = uint32_t(layers.size());
    end.layers = layers.empty() ? nullptr : layers.data();
    active = false;
    XrResult result = api.EndFrame(session, &end);
    // A rejected layer set loses one frame, not the session.
    if (result == XR_ERROR_POSE_INVALID || result == XR_ERROR_LAYER_INVALID ||
        result == XR_ERROR_SWAPCHAIN_RECT_INVALID) {
      static unsigned reports;
      if (reports++ < 8)
        api.log.format("xrEndFrame rejected %d with %u layers", int(result),
                       unsigned(layers.size()));
      return;
    }
    api.check(result, "xrEndFrame");
  }
};

// Controller actions, abstracted to an Xbox-style layout on the game side.
struct Actions {
  XrActionSet set = XR_NULL_HANDLE;
  XrPath hand[2]{};
  XrAction stick{}, trigger{}, squeeze{}, primary{}, secondary{}, thumb{}, menu{};
  XrPath path(Api &api, XrInstance instance, const char *name) {
    XrPath result = XR_NULL_PATH;
    api.check(api.StringToPath(instance, name, &result), "xrStringToPath");
    return result;
  }
  XrAction action(Api &api, const char *name, const char *label, XrActionType type) {
    XrActionCreateInfo create{XR_TYPE_ACTION_CREATE_INFO};
    std::snprintf(create.actionName, sizeof(create.actionName), "%s", name);
    std::snprintf(create.localizedActionName, sizeof(create.localizedActionName),
                  "%s", label);
    create.actionType = type;
    create.countSubactionPaths = 2;
    create.subactionPaths = hand;
    XrAction result = XR_NULL_HANDLE;
    api.check(api.CreateAction(set, &create, &result), "xrCreateAction");
    return result;
  }
  void create(Api &api, XrInstance instance, XrSession session) {
    hand[0] = path(api, instance, "/user/hand/left");
    hand[1] = path(api, instance, "/user/hand/right");
    XrActionSetCreateInfo create{XR_TYPE_ACTION_SET_CREATE_INFO};
    std::strcpy(create.actionSetName, "gurumin");
    std::strcpy(create.localizedActionSetName, "Gurumin");
    api.check(api.CreateActionSet(instance, &create, &set), "xrCreateActionSet");
    stick = action(api, "stick", "Move (left) / turn camera (right)",
                   XR_ACTION_TYPE_VECTOR2F_INPUT);
    trigger = action(api, "trigger", "Trigger (LT / RT)", XR_ACTION_TYPE_FLOAT_INPUT);
    squeeze = action(api, "grip", "Grip (LB / RB)", XR_ACTION_TYPE_FLOAT_INPUT);
    primary = action(api, "primary", "A / X button", XR_ACTION_TYPE_BOOLEAN_INPUT);
    secondary = action(api, "secondary", "B / Y button", XR_ACTION_TYPE_BOOLEAN_INPUT);
    thumb = action(api, "stick_click",
                   "Stick click (both sticks together recenter)",
                   XR_ACTION_TYPE_BOOLEAN_INPUT);
    menu = action(api, "menu", "Start (left) / Back (right)", XR_ACTION_TYPE_BOOLEAN_INPUT);
    suggest(api, instance);
    XrSessionActionSetsAttachInfo attach{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
    attach.countActionSets = 1;
    attach.actionSets = &set;
    api.check(api.AttachSessionActionSets(session, &attach), "xrAttachSessionActionSets");
  }
  void suggest(Api &api, XrInstance instance) {
    struct Binding {
      XrAction *action;
      int hand; // 0 left, 1 right, 2 both
      const char *component;
    };
    struct Profile {
      const char *name;
      std::vector<Binding> bindings;
    };
    const Profile profiles[] = {
        {"/interaction_profiles/oculus/touch_controller",
         {{&stick, 2, "thumbstick"}, {&trigger, 2, "trigger/value"},
          {&squeeze, 2, "squeeze/value"}, {&thumb, 2, "thumbstick/click"},
          {&primary, 0, "x/click"}, {&secondary, 0, "y/click"},
          {&primary, 1, "a/click"}, {&secondary, 1, "b/click"},
          {&menu, 0, "menu/click"}}},
        // Index squeeze/value is finger curl; the force sensor is the grip.
        {"/interaction_profiles/valve/index_controller",
         {{&stick, 2, "thumbstick"}, {&trigger, 2, "trigger/value"},
          {&squeeze, 2, "squeeze/force"}, {&thumb, 2, "thumbstick/click"},
          {&primary, 2, "a/click"}, {&secondary, 2, "b/click"},
          {&menu, 2, "trackpad/force"}}},
        {"/interaction_profiles/htc/vive_controller",
         {{&stick, 2, "trackpad"}, {&trigger, 2, "trigger/value"},
          {&squeeze, 2, "squeeze/click"}, {&thumb, 2, "trackpad/click"},
          {&primary, 2, "trigger/click"}, {&menu, 2, "menu/click"}}},
        {"/interaction_profiles/microsoft/motion_controller",
         {{&stick, 2, "thumbstick"}, {&trigger, 2, "trigger/value"},
          {&squeeze, 2, "squeeze/click"}, {&thumb, 2, "thumbstick/click"},
          {&primary, 2, "trackpad/click"}, {&menu, 2, "menu/click"}}},
        {"/interaction_profiles/khr/simple_controller",
         {{&primary, 2, "select/click"}, {&menu, 2, "menu/click"}}},
    };
    unsigned accepted = 0;
    for (const auto &profile : profiles) {
      std::vector<XrActionSuggestedBinding> bindings;
      for (const auto &b : profile.bindings)
        for (int h = 0; h < 2; ++h)
          if (b.hand == 2 || b.hand == h) {
            std::string name = std::string(h ? "/user/hand/right/input/"
                                             : "/user/hand/left/input/") +
                               b.component;
            bindings.push_back({*b.action, path(api, instance, name.c_str())});
          }
      XrInteractionProfileSuggestedBinding suggestion{
          XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
      suggestion.interactionProfile = path(api, instance, profile.name);
      suggestion.countSuggestedBindings = uint32_t(bindings.size());
      suggestion.suggestedBindings = bindings.data();
      XrResult result = api.SuggestInteractionProfileBindings(instance, &suggestion);
      if (XR_SUCCEEDED(result))
        ++accepted;
      else {
        api.log.format("Binding profile %s rejected: %d", profile.name, int(result));
        if (result != XR_ERROR_PATH_UNSUPPORTED)
          api.check(result, "xrSuggestInteractionProfileBindings");
      }
    }
    if (!accepted)
      throw std::runtime_error("Runtime rejected every controller profile");
  }
  XrActionStateGetInfo get(XrAction a, unsigned h) const {
    XrActionStateGetInfo info{XR_TYPE_ACTION_STATE_GET_INFO};
    info.action = a;
    info.subactionPath = hand[h];
    return info;
  }
  bool boolean(Api &api, XrSession session, XrAction a, unsigned h) {
    auto info = get(a, h);
    XrActionStateBoolean state{XR_TYPE_ACTION_STATE_BOOLEAN};
    api.check(api.GetActionStateBoolean(session, &info, &state), "xrGetActionStateBoolean");
    return state.isActive && state.currentState;
  }
  float value(Api &api, XrSession session, XrAction a, unsigned h) {
    auto info = get(a, h);
    XrActionStateFloat state{XR_TYPE_ACTION_STATE_FLOAT};
    api.check(api.GetActionStateFloat(session, &info, &state), "xrGetActionStateFloat");
    return state.isActive && std::isfinite(state.currentState)
               ? std::clamp(state.currentState, 0.f, 1.f)
               : 0.f;
  }
  void sample(Api &api, XrSession session, Input &input) {
    XrActiveActionSet active{set, XR_NULL_PATH};
    XrActionsSyncInfo sync{XR_TYPE_ACTIONS_SYNC_INFO};
    sync.countActiveActionSets = 1;
    sync.activeActionSets = &active;
    XrResult result = api.SyncActions(session, &sync);
    if (result == XR_SESSION_NOT_FOCUSED)
      return;
    api.check(result, "xrSyncActions");
    for (unsigned h = 0; h < 2; ++h) {
      auto info = get(stick, h);
      XrActionStateVector2f axis{XR_TYPE_ACTION_STATE_VECTOR2F};
      api.check(api.GetActionStateVector2f(session, &info, &axis),
                "xrGetActionStateVector2f");
      if (axis.isActive && std::isfinite(axis.currentState.x) &&
          std::isfinite(axis.currentState.y)) {
        input.stick[h][0] = std::clamp(axis.currentState.x, -1.f, 1.f);
        input.stick[h][1] = std::clamp(axis.currentState.y, -1.f, 1.f);
      }
      input.trigger[h] = value(api, session, trigger, h);
      input.squeeze[h] = value(api, session, squeeze, h);
      uint32_t buttons = 0;
      if (boolean(api, session, primary, h)) buttons |= Primary;
      if (boolean(api, session, secondary, h)) buttons |= Secondary;
      if (boolean(api, session, thumb, h)) buttons |= Thumb;
      if (boolean(api, session, menu, h)) buttons |= Menu;
      input.buttons[h] = buttons;
    }
  }
  void destroy(Api &api) noexcept {
    if (set)
      api.cleanup(api.DestroyActionSet(set), "xrDestroyActionSet");
    set = XR_NULL_HANDLE;
  }
};

struct Pending {
  bool active = false;
  uint64_t deadline = 0, sequence = 0;
};

struct Host {
  Log &log;
  Api api;
  Channel channel;
  Handle gameProcess;
  bool ownsChannel = false, running = false, quit = false;
  XrInstance instance = XR_NULL_HANDLE;
  XrSystemId system = XR_NULL_SYSTEM_ID;
  XrSession session = XR_NULL_HANDLE;
  XrSpace local = XR_NULL_HANDLE, viewSpace = XR_NULL_HANDLE;
  XrSessionState state = XR_SESSION_STATE_UNKNOWN;
  XrSystemProperties systemProperties{XR_TYPE_SYSTEM_PROPERTIES};
  std::array<XrViewConfigurationView, 2> viewConfiguration{};
  Com<ID3D11Device> device;
  Com<ID3D11DeviceContext> context;
  Com<ID3D11Query> drainQuery;
  Com<ID3D11Texture2D> sharedImage[2][ImageCount];
  uint32_t sharedOpened = 0, sharedTried = 0;
  SharedImages openedImages{};
  PixelStore pixels;
  Actions actions;
  Swapchain eye[2], hud;
  Pending outstanding[2];
  Request cachedRequest{};
  uint64_t cachedDeadline = 0;
  bool cachedWorld = false, cachedHud = false, cachedScreen = false;
  GameState game{};
  SoftFollow follow;
  uint32_t sessionGeneration = 0;
  uint64_t inputSequence = 0, requestSequence = 0;
  uint64_t completed = 0, zeroCopy = 0, pixelPairs = 0, screens = 0,
           expired = 0, imageTimeouts = 0, frames = 0;
  ULONGLONG lastDiagnostics = 0;
  LARGE_INTEGER lastFrame{};

  explicit Host(Log &l) : log(l), api(l) {}
  ~Host() {
    disconnect();
    drainGpu();
    for (auto &chain : eye)
      chain.destroy();
    hud.destroy();
    actions.destroy(api);
    if (viewSpace)
      api.cleanup(api.DestroySpace(viewSpace), "xrDestroySpace VIEW");
    if (local)
      api.cleanup(api.DestroySpace(local), "xrDestroySpace LOCAL");
    if (session)
      api.cleanup(api.DestroySession(session), "xrDestroySession");
    for (auto &row : sharedImage)
      for (auto &image : row)
        image.reset();
    drainQuery.reset();
    context.reset();
    device.reset();
    if (instance && api.DestroyInstance)
      api.cleanup(api.DestroyInstance(instance), "xrDestroyInstance");
  }
  bool drainGpu() noexcept {
    if (!context.p || !drainQuery.p)
      return true;
    context->End(drainQuery.p);
    context->Flush();
    ULONGLONG deadline = GetTickCount64() + 500;
    for (;;) {
      HRESULT result = context->GetData(drainQuery.p, nullptr, 0,
                                        D3D11_ASYNC_GETDATA_DONOTFLUSH);
      if (result == S_OK || FAILED(device->GetDeviceRemovedReason()))
        return true;
      if (FAILED(result) || GetTickCount64() >= deadline)
        return false;
      Sleep(0);
    }
  }
  void connect(const std::wstring &token) {
    if (!channel.open(token, false))
      throw std::runtime_error("IPC channel missing or incompatible");
    Mutex lock(channel, 200);
    if (!lock)
      throw std::runtime_error("IPC channel busy during connect");
    auto &s = *channel.shared;
    if (!s.gamePid || s.hostPid || s.shutdown)
      throw std::runtime_error("IPC channel stale or already owned");
    gameProcess.h = OpenProcess(SYNCHRONIZE, FALSE, s.gamePid);
    if (!gameProcess.h || WaitForSingleObject(gameProcess.h, 0) != WAIT_TIMEOUT)
      throw std::runtime_error("Game process unavailable");
    s.hostPid = GetCurrentProcessId();
    ownsChannel = true;
  }
  void disconnect() noexcept {
    if (!ownsChannel)
      return;
    try {
      Mutex lock(channel, 50);
      if (lock && channel.shared->hostPid == GetCurrentProcessId()) {
        channel.shared->latest = Input{};
        channel.shared->hostReady = 0;
        channel.shared->hostPid = 0;
        for (auto &slot : channel.shared->slot) {
          if (slot.state == SlotState::Rendering)
            slot.cancelled = 1;
          else {
            slot.state = SlotState::Empty;
            slot.cancelled = 0;
          }
        }
      }
    } catch (...) {
    }
    ownsChannel = false;
  }
  void initialize(const std::wstring &directory) {
    bool wine = GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "wine_get_version") != nullptr;
    log.write(wine ? "Wine/Proton: OpenXR goes through Proton's wineopenxr bridge"
                   : "Windows: using the packaged OpenXR loader");
    api.open(directory);
    uint32_t count = 0;
    api.check(api.EnumerateInstanceExtensionProperties(nullptr, 0, &count, nullptr),
              "xrEnumerateInstanceExtensionProperties count");
    std::vector<XrExtensionProperties> extensions(count, {XR_TYPE_EXTENSION_PROPERTIES});
    api.check(api.EnumerateInstanceExtensionProperties(nullptr, count, &count,
                                                       extensions.data()),
              "xrEnumerateInstanceExtensionProperties");
    bool d3d11 = std::any_of(extensions.begin(), extensions.end(), [](const auto &e) {
      return !std::strcmp(e.extensionName, XR_KHR_D3D11_ENABLE_EXTENSION_NAME);
    });
    if (!d3d11) {
      if (wine)
        log.write("Proton: add PRESSURE_VESSEL_IMPORT_OPENXR_1_RUNTIMES=1 to the "
                  "launch options and start Monado/SteamVR before the game");
      throw std::runtime_error("Runtime does not offer XR_KHR_D3D11_enable");
    }
    const char *enabled[] = {XR_KHR_D3D11_ENABLE_EXTENSION_NAME};
    XrInstanceCreateInfo create{XR_TYPE_INSTANCE_CREATE_INFO};
    std::strcpy(create.applicationInfo.applicationName, "Gurumin VR");
    std::strcpy(create.applicationInfo.engineName, "Gurumin Enhanced Rendering");
    create.applicationInfo.applicationVersion = create.applicationInfo.engineVersion = 1;
    create.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);
    create.enabledExtensionCount = 1;
    create.enabledExtensionNames = enabled;
    api.check(api.CreateInstance(&create, &instance), "xrCreateInstance");
    api.instanceFunctions(instance);
    XrInstanceProperties runtime{XR_TYPE_INSTANCE_PROPERTIES};
    if (XR_SUCCEEDED(api.GetInstanceProperties(instance, &runtime)))
      log.format("OpenXR runtime: %s %u.%u.%u", runtime.runtimeName,
                 XR_VERSION_MAJOR(runtime.runtimeVersion),
                 XR_VERSION_MINOR(runtime.runtimeVersion),
                 XR_VERSION_PATCH(runtime.runtimeVersion));
    XrSystemGetInfo get{XR_TYPE_SYSTEM_GET_INFO};
    get.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    api.check(api.GetSystem(instance, &get, &system), "xrGetSystem HMD");
    api.check(api.GetSystemProperties(instance, system, &systemProperties),
              "xrGetSystemProperties");
    log.format("System: %s maxLayers=%u", systemProperties.systemName,
               systemProperties.graphicsProperties.maxLayerCount);
    if (systemProperties.graphicsProperties.maxLayerCount < 2)
      throw std::runtime_error("Runtime cannot present a projection and a quad layer");
    api.check(api.EnumerateViewConfigurationViews(
                  instance, system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0,
                  &count, nullptr),
              "xrEnumerateViewConfigurationViews count");
    if (count != 2)
      throw std::runtime_error("PRIMARY_STEREO must contain two views");
    for (auto &v : viewConfiguration)
      v = {XR_TYPE_VIEW_CONFIGURATION_VIEW};
    api.check(api.EnumerateViewConfigurationViews(
                  instance, system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 2,
                  &count, viewConfiguration.data()),
              "xrEnumerateViewConfigurationViews");
    for (const auto &v : viewConfiguration)
      log.format("View recommended %ux%u max %ux%u", v.recommendedImageRectWidth,
                 v.recommendedImageRectHeight, v.maxImageRectWidth,
                 v.maxImageRectHeight);
    graphics();
    XrGraphicsBindingD3D11KHR binding{XR_TYPE_GRAPHICS_BINDING_D3D11_KHR};
    binding.device = device.p;
    XrSessionCreateInfo sessionInfo{XR_TYPE_SESSION_CREATE_INFO};
    sessionInfo.next = &binding;
    sessionInfo.systemId = system;
    XrResult sessionResult = api.CreateSession(instance, &sessionInfo, &session);
    if (XR_FAILED(sessionResult) && wine)
      log.write("Proton's D3D11 OpenXR bridge requires DXVK (not WineD3D)");
    api.check(sessionResult, "xrCreateSession D3D11");
    XrReferenceSpaceCreateInfo space{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    space.poseInReferenceSpace.orientation.w = 1;
    space.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    api.check(api.CreateReferenceSpace(session, &space, &local), "xrCreateReferenceSpace LOCAL");
    space.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    api.check(api.CreateReferenceSpace(session, &space, &viewSpace), "xrCreateReferenceSpace VIEW");
    actions.create(api, instance, session);
    api.check(api.EnumerateSwapchainFormats(session, 0, &count, nullptr),
              "xrEnumerateSwapchainFormats count");
    std::vector<int64_t> formats(count);
    api.check(api.EnumerateSwapchainFormats(session, count, &count, formats.data()),
              "xrEnumerateSwapchainFormats");
    // The game writes sRGB-encoded bytes. sRGB swapchains make the runtime
    // decode them correctly; the copies stay byte-for-byte (same typeless
    // family). UNORM is the fallback, which the runtime treats as linear.
    const DXGI_FORMAT preferred[] = {DXGI_FORMAT_B8G8R8A8_UNORM_SRGB, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
                                     DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM};
    bool found = false;
    for (DXGI_FORMAT format : preferred)
      if (std::find(formats.begin(), formats.end(), int64_t(format)) != formats.end()) {
        api.colorFormat = format;
        found = true;
        break;
      }
    if (!found)
      throw std::runtime_error("Runtime offers no 8-bit BGRA or RGBA swapchain format");
    log.format("Swapchain format %d", int(api.colorFormat));
    // Publish the recommended eye size early: the game sizes its renderer
    // from it before creating its D3D9 device.
    {
      Mutex lock(channel, 100);
      if (lock) {
        auto size = recommendedEye();
        channel.shared->recommendedWidth = size.first;
        channel.shared->recommendedHeight = size.second;
        channel.shared->hostReady = 1;
      }
    }
    log.write("OpenXR D3D11 session initialized");
  }
  void graphics() {
    XrGraphicsRequirementsD3D11KHR requirements{XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR};
    api.check(api.GetD3D11GraphicsRequirementsKHR(instance, system, &requirements),
              "xrGetD3D11GraphicsRequirementsKHR");
    Com<IDXGIFactory1> factory;
    hresult(CreateDXGIFactory1(__uuidof(IDXGIFactory1),
                               reinterpret_cast<void **>(factory.put())),
            "CreateDXGIFactory1", log);
    Com<IDXGIAdapter1> adapter;
    for (UINT i = 0;; ++i) {
      HRESULT result = factory->EnumAdapters1(i, adapter.put());
      if (result == DXGI_ERROR_NOT_FOUND)
        throw std::runtime_error("OpenXR adapter LUID not found");
      hresult(result, "EnumAdapters1", log);
      DXGI_ADAPTER_DESC1 desc{};
      hresult(adapter->GetDesc1(&desc), "GetDesc1", log);
      if (desc.AdapterLuid.LowPart == requirements.adapterLuid.LowPart &&
          desc.AdapterLuid.HighPart == requirements.adapterLuid.HighPart)
        break;
    }
    const D3D_FEATURE_LEVEL available[] = {
        D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0, D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
    std::vector<D3D_FEATURE_LEVEL> levels;
    for (auto level : available)
      if (level >= requirements.minFeatureLevel)
        levels.push_back(level);
    if (levels.empty())
      throw std::runtime_error("Runtime's D3D feature level is unsupported");
    D3D_FEATURE_LEVEL selected{};
    HRESULT result = D3D11CreateDevice(adapter.p, D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                                       D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels.data(),
                                       UINT(levels.size()), D3D11_SDK_VERSION,
                                       device.put(), &selected, context.put());
    while (result == E_INVALIDARG && levels.size() > 1) {
      levels.erase(levels.begin());
      result = D3D11CreateDevice(adapter.p, D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                                 D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels.data(),
                                 UINT(levels.size()), D3D11_SDK_VERSION,
                                 device.put(), &selected, context.put());
    }
    hresult(result, "D3D11CreateDevice", log);
    D3D11_QUERY_DESC query{};
    query.Query = D3D11_QUERY_EVENT;
    hresult(device->CreateQuery(&query, drainQuery.put()), "CreateQuery", log);
  }
  std::pair<uint32_t, uint32_t> recommendedEye() const {
    uint32_t w = 0, h = 0;
    for (const auto &v : viewConfiguration) {
      w = std::max(w, v.recommendedImageRectWidth);
      h = std::max(h, v.recommendedImageRectHeight);
    }
    return {std::min(w, MaxDimension), std::min(h, MaxDimension)};
  }
  void events() {
    for (;;) {
      XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
      XrResult result = api.PollEvent(instance, &event);
      if (result == XR_EVENT_UNAVAILABLE)
        break;
      api.check(result, "xrPollEvent");
      if (event.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) {
        log.write("OpenXR instance loss pending");
        quit = true;
      } else if (event.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
        const auto &change = *reinterpret_cast<const XrEventDataSessionStateChanged *>(&event);
        if (change.session != session)
          continue;
        state = change.state;
        log.format("OpenXR session state %d", int(state));
        if (state != XR_SESSION_STATE_FOCUSED)
          invalidate();
        if (state == XR_SESSION_STATE_READY && !running) {
          XrSessionBeginInfo begin{XR_TYPE_SESSION_BEGIN_INFO};
          begin.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
          api.check(api.BeginSession(session, &begin), "xrBeginSession");
          running = true;
          ++sessionGeneration;
        } else if (state == XR_SESSION_STATE_STOPPING && running) {
          for (auto &chain : eye)
            chain.releaseAcquired();
          hud.releaseAcquired();
          api.check(api.EndSession(session), "xrEndSession");
          running = false;
        } else if (state == XR_SESSION_STATE_EXITING ||
                   state == XR_SESSION_STATE_LOSS_PENDING)
          quit = true;
      }
    }
  }
  void invalidate() {
    cachedWorld = cachedHud = cachedScreen = false;
    for (auto &p : outstanding)
      p.deadline = 0; // Expire; reaped under the lock.
    follow.reset();
  }
  // Retire our requests that are too old; never touch a Rendering slot
  // except to cancel it (only the game may finish Rendering).
  void reapLocked() {
    ULONGLONG now = GetTickCount64();
    for (unsigned i = 0; i < 2; ++i) {
      auto &slot = channel.shared->slot[i];
      auto &pending = outstanding[i];
      if (!pending.active) {
        continue;
      }
      if (slot.state == SlotState::Empty) {
        pending = {};
        continue;
      }
      if (slot.request.sequence != pending.sequence)
        throw std::runtime_error("IPC slot identity changed");
      if (now >= pending.deadline) {
        if (slot.state == SlotState::Rendering)
          slot.cancelled = 1;
        else {
          slot.state = SlotState::Empty;
          slot.cancelled = 0;
          pending = {};
          ++expired;
        }
      }
    }
  }
  void openSharedImages(SharedImages &images) {
    if (!device.p || images.generation == sharedTried) {
      images.opened = images.generation && images.generation == sharedOpened ? sharedOpened : 0;
      return;
    }
    sharedTried = images.generation;
    for (auto &row : sharedImage)
      for (auto &image : row)
        image.reset();
    sharedOpened = 0;
    images.opened = 0;
    if (!images.generation)
      return;
    // Shared images are BGRA; a GPU copy needs a BGRA-family swapchain.
    bool ok = api.colorFormat == DXGI_FORMAT_B8G8R8A8_UNORM ||
              api.colorFormat == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    HRESULT failure = S_OK;
    for (unsigned slot = 0; slot < 2 && ok; ++slot)
      for (unsigned i = 0; i < ImageCount && ok; ++i) {
        HANDLE handle = reinterpret_cast<HANDLE>(uintptr_t(images.handle[slot][i]));
        failure = device->OpenSharedResource(handle, __uuidof(ID3D11Texture2D),
                                             reinterpret_cast<void **>(sharedImage[slot][i].put()));
        ok = SUCCEEDED(failure) && sharedImage[slot][i].p;
        if (ok) {
          D3D11_TEXTURE2D_DESC d{};
          sharedImage[slot][i]->GetDesc(&d);
          uint32_t w = i == HudImage ? images.hudWidth : images.width;
          uint32_t h = i == HudImage ? images.hudHeight : images.height;
          ok = d.Width == w && d.Height == h && d.MipLevels == 1 && d.ArraySize == 1 &&
               d.SampleDesc.Count == 1 &&
               (d.Format == DXGI_FORMAT_B8G8R8A8_UNORM || d.Format == DXGI_FORMAT_B8G8R8A8_TYPELESS);
        }
      }
    if (!ok) {
      for (auto &row : sharedImage)
        for (auto &image : row)
          image.reset();
      log.format("Shared images generation=%u unavailable hr=%08lx; CPU transfer",
                 images.generation, (unsigned long)failure);
      return;
    }
    sharedOpened = images.generation;
    openedImages = images;
    images.opened = sharedOpened;
    log.format("Shared images generation=%u eye=%ux%u hud=%ux%u opened: zero-copy",
               images.generation, images.width, images.height, images.hudWidth,
               images.hudHeight);
  }
  bool publish(const Input &input) {
    Mutex lock(channel, 1);
    if (!lock)
      return false;
    auto &shared = *channel.shared;
    if (shared.magic != Magic || shared.abi != Abi || shared.hostPid != GetCurrentProcessId())
      throw std::runtime_error("IPC header or host ownership changed");
    if (shared.shutdown) {
      quit = true;
      return false;
    }
    shared.latest = input;
    auto size = recommendedEye();
    shared.recommendedWidth = size.first;
    shared.recommendedHeight = size.second;
    openSharedImages(shared.images);
    game = shared.game;
    reapLocked();
    return true;
  }
  bool gameReady() const {
    ULONGLONG now = GetTickCount64();
    return game.rendering && game.tickMs && now >= game.tickMs &&
           now - game.tickMs <= GameStateAgeMs && game.eyeWidth && game.eyeHeight &&
           game.eyeWidth <= MaxDimension && game.eyeHeight <= MaxDimension &&
           game.hudWidth && game.hudHeight;
  }
  void ensureChains() {
    const auto &g = systemProperties.graphicsProperties;
    uint32_t w = std::min({game.eyeWidth, g.maxSwapchainImageWidth, viewConfiguration[0].maxImageRectWidth});
    uint32_t h = std::min({game.eyeHeight, g.maxSwapchainImageHeight, viewConfiguration[0].maxImageRectHeight});
    if (w != game.eyeWidth || h != game.eyeHeight)
      throw std::runtime_error("Game eye size exceeds runtime limits");
    if (eye[0].width != w || eye[0].height != h || !eye[1].handle) {
      invalidate();
      drainGpu();
      for (unsigned i = 0; i < 2; ++i) {
        eye[i].destroy();
        eye[i].create(api, session, w, h);
      }
    }
    if (hud.width != game.hudWidth || hud.height != game.hudHeight) {
      cachedHud = cachedScreen = false;
      drainGpu();
      hud.destroy();
      hud.create(api, session, game.hudWidth, game.hudHeight);
    }
  }
  bool viewsAt(XrTime time, std::array<XrView, 2> &views) {
    for (auto &view : views)
      view = {XR_TYPE_VIEW};
    XrViewLocateInfo locate{XR_TYPE_VIEW_LOCATE_INFO};
    locate.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    locate.displayTime = time;
    locate.space = local;
    XrViewState validity{XR_TYPE_VIEW_STATE};
    uint32_t count = 0;
    api.check(api.LocateViews(session, &locate, &validity, 2, &count, views.data()),
              "xrLocateViews");
    constexpr auto needed = XR_VIEW_STATE_ORIENTATION_VALID_BIT | XR_VIEW_STATE_POSITION_VALID_BIT;
    if (count != 2 || (validity.viewStateFlags & needed) != needed)
      return false;
    for (const auto &view : views) {
      const auto &f = view.fov;
      if (!finite(pose(view.pose)) || !(f.angleRight > f.angleLeft) ||
          !(f.angleUp > f.angleDown) || std::abs(f.angleLeft) >= Pi / 2 ||
          std::abs(f.angleRight) >= Pi / 2 || std::abs(f.angleUp) >= Pi / 2 ||
          std::abs(f.angleDown) >= Pi / 2)
        return false;
    }
    return true;
  }
  Input sample(XrTime time) {
    Input input{};
    input.sequence = ++inputSequence;
    input.tickMs = GetTickCount64();
    input.session = sessionGeneration;
    input.focused = state == XR_SESSION_STATE_FOCUSED;
    XrSpaceLocation head{XR_TYPE_SPACE_LOCATION};
    api.check(api.LocateSpace(viewSpace, local, time, &head), "xrLocateSpace VIEW");
    constexpr auto needed = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_POSITION_VALID_BIT;
    if ((head.locationFlags & needed) == needed && finite(pose(head.pose))) {
      input.head = pose(head.pose);
      input.headValid = 1;
    }
    if (input.focused)
      actions.sample(api, session, input);
    return input;
  }
  // Newest Ready pair whose GPU work finished; older Ready pairs retire.
  void pollEyes() {
    int candidate = -1;
    Request reply{};
    uint32_t content = 0, sharedGeneration = 0;
    bool pixelsOk = false;
    {
      Mutex lock(channel, 1);
      if (!lock)
        return;
      reapLocked();
      auto &shared = *channel.shared;
      for (int i = 0; i < 2; ++i)
        if (outstanding[i].active && shared.slot[i].state == SlotState::Ready &&
            shared.slot[i].gpuReady &&
            (candidate < 0 || shared.slot[i].request.sequence > shared.slot[candidate].request.sequence))
          candidate = i;
      if (candidate < 0)
        return;
      auto &older = shared.slot[1 - candidate];
      if (outstanding[1 - candidate].active && older.state == SlotState::Ready &&
          older.request.sequence < shared.slot[candidate].request.sequence) {
        older.state = SlotState::Empty;
        outstanding[1 - candidate] = {};
      }
      auto &slot = shared.slot[candidate];
      reply = slot.request;
      content = slot.content;
      sharedGeneration = slot.sharedImage;
      if (!sharedGeneration) {
        const auto &published = shared.pixels;
        if (!pixels.matches(published) &&
            !pixels.open(channel.token, published, false))
          log.write("Pixel sections unavailable; pair skipped");
        pixelsOk = pixels.matches(published) && slot.pixelGeneration == published.generation;
      }
      // The game reuses this slot only after we request it again.
      slot.state = SlotState::Empty;
      slot.cancelled = 0;
      slot.gpuReady = slot.sharedImage = 0;
      outstanding[candidate] = {};
    }
    bool zero = sharedGeneration && sharedGeneration == sharedOpened;
    if (!zero && !pixelsOk)
      return;
    bool world = (content & World) && reply.width == eye[0].width &&
                 reply.height == eye[0].height &&
                 (!zero || (openedImages.width == eye[0].width && openedImages.height == eye[0].height));
    bool overlay = (content & (Hud | Screen)) &&
                   (zero ? openedImages.hudWidth == hud.width && openedImages.hudHeight == hud.height
                         : pixels.hudWidth == hud.width && pixels.hudHeight == hud.height);
    if (world && (!eye[0].waitImage() || !eye[1].waitImage())) {
      ++imageTimeouts;
      world = false;
    }
    if (overlay && !hud.waitImage()) {
      ++imageTimeouts;
      overlay = false;
    }
    if (world) {
      for (unsigned h = 0; h < 2; ++h) {
        if (zero)
          context->CopySubresourceRegion(eye[h].target(), 0, 0, 0, 0,
                                         sharedImage[candidate][h].p, 0, nullptr);
        else
          eye[h].upload(context.p, pixels.view[pixelBuffer(candidate, h)]);
      }
    }
    if (overlay) {
      if (zero)
        context->CopySubresourceRegion(hud.target(), 0, 0, 0, 0,
                                       sharedImage[candidate][HudImage].p, 0, nullptr);
      else
        hud.upload(context.p, pixels.view[pixelBuffer(candidate, HudImage)]);
    }
    // Copies from shared images must finish before the game may render into
    // them again, which it can as soon as this thread requests the slot.
    if ((world || overlay) && probeDue()) {
      lastProbe = GetTickCount64();
      if (world)
        probe(eye[0].target(), "eye0");
      if (overlay)
        probe(hud.target(), content & Screen ? "screen" : "hud");
      if (overlay && zero)
        probe(sharedImage[candidate][HudImage].p, "overlay-source");
    }
    if ((world || overlay) && zero && !drainGpu())
      throw std::runtime_error("Shared image copy did not complete");
    if (world) {
      eye[0].release();
      eye[1].release();
    }
    if (overlay)
      hud.release();
    hresult(device->GetDeviceRemovedReason(), "D3D11 device removed", log);
    ULONGLONG now = GetTickCount64();
    if (content & Screen) {
      cachedScreen = overlay;
      cachedWorld = cachedHud = false;
      ++screens;
    } else {
      if (world) {
        cachedWorld = true;
        cachedRequest = reply;
      }
      cachedHud = overlay && (content & Hud);
      cachedScreen = false;
    }
    cachedDeadline = now + CachedHoldMs;
    ++completed;
    ++(zero ? zeroCopy : pixelPairs);
  }
  void requestEyes(const Request &request) {
    Mutex lock(channel, 2);
    if (!lock)
      return;
    reapLocked();
    auto &shared = *channel.shared;
    ULONGLONG deadline = GetTickCount64() + FrameAgeMs;
    // Late-latch a request the game has not taken yet with fresher poses.
    unsigned active = 0;
    for (unsigned i = 0; i < 2; ++i) {
      if (!outstanding[i].active)
        continue;
      ++active;
      if (shared.slot[i].cancelled)
        return;
      if (shared.slot[i].state == SlotState::Requested) {
        shared.slot[i].request = request;
        outstanding[i] = {true, deadline, request.sequence};
        return;
      }
    }
    if (active > 1)
      return;
    for (unsigned i = 0; i < 2; ++i)
      if (!outstanding[i].active && shared.slot[i].state == SlotState::Empty) {
        auto &slot = shared.slot[i];
        slot.request = request;
        slot.cancelled = slot.content = slot.sharedImage = slot.gpuReady = 0;
        slot.state = SlotState::Requested;
        outstanding[i] = {true, deadline, request.sequence};
        return;
      }
  }
  // Debug probe, enabled by a "probe" file beside the host: every 5 s log the
  // mean colour of the images sent to the runtime and save them as BMPs.
  ULONGLONG lastProbe = 0;
  int probing = -1;
  bool probeDue() {
    if (probing < 0)
      probing = GetFileAttributesW((executableDirectory() + L"probe").c_str()) !=
                INVALID_FILE_ATTRIBUTES;
    return probing && GetTickCount64() - lastProbe >= 5000;
  }
  void probe(ID3D11Texture2D *source, const char *name) {
    D3D11_TEXTURE2D_DESC d{};
    source->GetDesc(&d);
    d.Usage = D3D11_USAGE_STAGING;
    d.BindFlags = 0;
    d.MiscFlags = 0;
    d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    Com<ID3D11Texture2D> staging;
    if (FAILED(device->CreateTexture2D(&d, nullptr, staging.put())))
      return log.format("probe %s: staging failed", name);
    context->CopyResource(staging.p, source);
    D3D11_MAPPED_SUBRESOURCE m{};
    if (FAILED(context->Map(staging.p, 0, D3D11_MAP_READ, 0, &m)))
      return log.format("probe %s: map failed", name);
    double sum[4]{};
    uint64_t count = 0;
    for (UINT y = 0; y < d.Height; y += 8)
      for (UINT x = 0; x < d.Width; x += 8, ++count) {
        auto px = static_cast<const uint8_t *>(m.pData) + y * m.RowPitch + x * 4;
        for (int c = 0; c < 4; ++c)
          sum[c] += px[c];
      }
    log.format("probe %s %ux%u fmt=%u mean b=%.1f g=%.1f r=%.1f a=%.1f", name, d.Width,
               d.Height, unsigned(d.Format), sum[0] / count, sum[1] / count, sum[2] / count,
               sum[3] / count);
    std::wstring path = executableDirectory() + L"probe-" +
                        std::wstring(name, name + std::strlen(name)) + L".bmp";
    Handle file;
    file.h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file.h != INVALID_HANDLE_VALUE) {
      BITMAPINFOHEADER info{sizeof(info), LONG(d.Width), -LONG(d.Height), 1, 32, BI_RGB};
      BITMAPFILEHEADER header{0x4d42, DWORD(sizeof(BITMAPFILEHEADER) + sizeof(info) + d.Width * d.Height * 4),
                              0, 0, DWORD(sizeof(BITMAPFILEHEADER) + sizeof(info))};
      DWORD n;
      WriteFile(file.h, &header, sizeof(header), &n, nullptr);
      WriteFile(file.h, &info, sizeof(info), &n, nullptr);
      for (UINT y = 0; y < d.Height; ++y)
        WriteFile(file.h, static_cast<const uint8_t *>(m.pData) + y * m.RowPitch, d.Width * 4, &n,
                  nullptr);
    }
    context->Unmap(staging.p, 0);
  }
  void diagnostics() {
    ULONGLONG now = GetTickCount64();
    if (now - lastDiagnostics < 5000)
      return;
    lastDiagnostics = now;
    log.format("frames=%llu pairs=%llu zeroCopy=%llu pixel=%llu screens=%llu expired=%llu "
               "imageTimeouts=%llu world=%d hud=%d screen=%d",
               (unsigned long long)frames, (unsigned long long)completed,
               (unsigned long long)zeroCopy, (unsigned long long)pixelPairs,
               (unsigned long long)screens, (unsigned long long)expired,
               (unsigned long long)imageTimeouts, int(cachedWorld), int(cachedHud),
               int(cachedScreen));
  }
  void run() {
    while (!quit && !api.lossPending) {
      if (WaitForSingleObject(gameProcess.h, 0) != WAIT_TIMEOUT)
        break;
      events();
      if (quit || api.lossPending)
        break;
      if (!running) {
        Input idle{};
        idle.sequence = ++inputSequence;
        idle.tickMs = GetTickCount64();
        publish(idle);
        Sleep(10);
        continue;
      }
      XrFrameWaitInfo wait{XR_TYPE_FRAME_WAIT_INFO};
      XrFrameState frameState{XR_TYPE_FRAME_STATE};
      api.check(api.WaitFrame(session, &wait, &frameState), "xrWaitFrame");
      XrFrameBeginInfo begin{XR_TYPE_FRAME_BEGIN_INFO};
      api.check(api.BeginFrame(session, &begin), "xrBeginFrame");
      Frame frame(api, session, frameState.predictedDisplayTime);
      ++frames;
      LARGE_INTEGER now{}, frequency{};
      QueryPerformanceCounter(&now);
      QueryPerformanceFrequency(&frequency);
      float dt = lastFrame.QuadPart ? float(double(now.QuadPart - lastFrame.QuadPart) / frequency.QuadPart) : 0.f;
      lastFrame = now;
      std::array<XrView, 2> views;
      bool validViews = viewsAt(frameState.predictedDisplayTime, views);
      Input input = sample(frameState.predictedDisplayTime);
      std::vector<const XrCompositionLayerBaseHeader *> layers;
      XrCompositionLayerProjection projection{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
      std::array<XrCompositionLayerProjectionView, 2> projectionViews{};
      XrCompositionLayerQuad quad{XR_TYPE_COMPOSITION_LAYER_QUAD};
      bool published = publish(input);
      if (published && frameState.shouldRender && gameReady()) {
        ensureChains();
        if (validViews && input.focused && input.headValid) {
          Request request{};
          request.sequence = ++requestSequence;
          request.predictedTime = frameState.predictedDisplayTime;
          request.session = sessionGeneration;
          request.width = game.eyeWidth;
          request.height = game.eyeHeight;
          request.input = input;
          for (unsigned h = 0; h < 2; ++h) {
            request.eye[h] = pose(views[h].pose);
            const auto &f = views[h].fov;
            request.fov[h] = {f.angleLeft, f.angleRight, f.angleUp, f.angleDown};
          }
          pollEyes();
          requestEyes(request);
        } else
          pollEyes();
        if (GetTickCount64() >= cachedDeadline)
          cachedWorld = cachedHud = cachedScreen = false;
        if (input.headValid)
          follow.update(input.head, dt);
        if (cachedScreen && hud.ownership.previousImageAvailable() && follow.initialized) {
          float width = game.screenWidthMeters;
          quad.space = local;
          quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
          quad.subImage = hud.subimage();
          quad.pose = xrPose(follow.panel(game.screenDistance));
          quad.size = {width, width * hud.height / float(hud.width)};
          layers.push_back(reinterpret_cast<const XrCompositionLayerBaseHeader *>(&quad));
          if (probing > 0 && frames % 500 == 0)
            log.format("screen quad pos=%.2f,%.2f,%.2f size=%.2fx%.2f head=%.2f,%.2f,%.2f",
                       quad.pose.position.x, quad.pose.position.y, quad.pose.position.z,
                       quad.size.width, quad.size.height, input.head.p.x, input.head.p.y,
                       input.head.p.z);
        } else {
          if (cachedWorld && eye[0].ownership.previousImageAvailable() &&
              eye[1].ownership.previousImageAvailable()) {
            projection.space = local;
            for (unsigned h = 0; h < 2; ++h) {
              auto &view = projectionViews[h];
              view = {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};
              view.pose = xrPose(cachedRequest.eye[h]);
              const auto &f = cachedRequest.fov[h];
              view.fov = {f.left, f.right, f.up, f.down};
              view.subImage = eye[h].subimage();
            }
            projection.viewCount = 2;
            projection.views = projectionViews.data();
            layers.push_back(reinterpret_cast<const XrCompositionLayerBaseHeader *>(&projection));
          }
          if (cachedHud && hud.ownership.previousImageAvailable() && follow.initialized) {
            float width = game.hudWidthMeters;
            quad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
            quad.space = local;
            quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
            quad.subImage = hud.subimage();
            quad.pose = xrPose(follow.panel(game.hudDistance));
            quad.size = {width, width * hud.height / float(hud.width)};
            layers.push_back(reinterpret_cast<const XrCompositionLayerBaseHeader *>(&quad));
          }
        }
      } else if (!gameReady())
        invalidate();
      frame.submit(layers);
      diagnostics();
    }
    log.write("Host stopped");
  }
};
} // namespace

int wmain(int argc, wchar_t **argv) {
  if (argc != 3 || std::wcscmp(argv[1], L"--channel") || !argv[2][0])
    return 2;
  std::wstring token = argv[2];
  if (token.size() > 64 ||
      !std::all_of(token.begin(), token.end(), [](wchar_t c) {
        return (c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'z') ||
               (c >= L'A' && c <= L'Z') || c == L'-';
      }))
    return 2;
  try {
    std::wstring directory = executableDirectory();
    Log log(directory);
    Host host(log);
    try {
      host.connect(token);
      host.initialize(directory);
      host.run();
    } catch (const std::exception &error) {
      log.write(error.what());
      if (host.channel.shared)
        host.channel.shared->error = 1;
      return 1;
    }
    return 0;
  } catch (const std::exception &) {
    return 1;
  }
}
