#define WIN32_LEAN_AND_MEAN
#include "MinHook.h"
#include "affine.hpp"
#include "rhythm.hpp"
#include "settings.hpp"
#include "filtering.hpp"
#include "antialias.hpp"
#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <d3d9.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <wincrypt.h>
#include <windows.h>
static FILE *logFile;
static bool interpolateMotion = true, smoothRhythm = true, borderless = true,
            diagnostics = false;
static bool windowedDevice;
static gurumin::ModernSettings modernSettings;
static gurumin::SceneAA sceneAA;
static void log(const char *fmt, ...) {
  if (!logFile)
    logFile = fopen("GuruminModern.log", "w");
  if (!logFile)
    return;
  va_list a;
  va_start(a, fmt);
  vfprintf(logFile, fmt, a);
  va_end(a);
  fputc('\n', logFile);
  fflush(logFile);
}
template <class T> static void hook(void *obj, int slot, T fn, T &orig) {
  auto v = *reinterpret_cast<void ***>(obj);
  if (v[slot] == reinterpret_cast<void *>(fn))
    return;
  DWORD p;
  if (!VirtualProtect(v + slot, sizeof(void *), PAGE_READWRITE, &p))
    return;
  orig = reinterpret_cast<T>(v[slot]);
  v[slot] = reinterpret_cast<void *>(fn);
  DWORD q;
  VirtualProtect(v + slot, sizeof(void *), p, &q);
}
using CreateDeviceFn = HRESULT(WINAPI *)(IDirect3D9 *, UINT, D3DDEVTYPE, HWND,
                                         DWORD, D3DPRESENT_PARAMETERS *,
                                         IDirect3DDevice9 **);
using PresentFn = HRESULT(WINAPI *)(IDirect3DDevice9 *, const RECT *,
                                    const RECT *, HWND, const RGNDATA *);
using TransformFn = HRESULT(WINAPI *)(IDirect3DDevice9 *, D3DTRANSFORMSTATETYPE,
                                      const D3DMATRIX *);
using DrawUPFn = HRESULT(WINAPI *)(IDirect3DDevice9 *, D3DPRIMITIVETYPE, UINT,
                                   const void *, UINT);
using ShaderConstFn = HRESULT(WINAPI *)(IDirect3DDevice9 *, UINT, const float *,
                                        UINT);
static CreateDeviceFn createDevice;
static PresentFn present;
static TransformFn setTransform;
static DrawUPFn drawUP;
static ShaderConstFn setConst;
static unsigned frames, draws, transforms, consts;
static uintptr_t gameBase;
static bool supported;
#include "crash_trace.hpp"
static unsigned outputWidth, outputHeight;
static void effectiveResolution(gurumin::Resolution);
static bool setResolutionTextureLimit(unsigned);
static unsigned codeImmediate(uintptr_t);
static HWND gameWindow;
static bool windowSized;
static LARGE_INTEGER frequency, start;
static double nowSeconds() {
  LARGE_INTEGER t;
  QueryPerformanceCounter(&t);
  return double(t.QuadPart) / frequency.QuadPart;
}
static double sharedFrameTime, sharedTickStamp;
static unsigned sharedFrame = ~0u, sharedTick = ~0u;
static double renderTime() {
  if (sharedFrame != frames) {
    sharedFrame = frames;
    sharedFrameTime = nowSeconds();
    unsigned tick =
        gameBase ? *reinterpret_cast<unsigned *>(gameBase + 0x1de47f4) : 0;
    if (tick != sharedTick) {
      sharedTick = tick;
      sharedTickStamp = sharedFrameTime;
    }
  }
  return sharedFrameTime;
}
template <class T> static T &game(uintptr_t rva) {
  return *reinterpret_cast<T *>(gameBase + rva);
}
static int hudDepth, worldDepth, cursorDepth;
static int projectedGeometryDepth;
static D3DVIEWPORT9 actualViewport{};
#include "shadows.hpp"
static bool hudSeen;
static unsigned lastHudFrame;
struct SkinKey {
  const float *bones;
  const float *model;
  int count;
  bool operator==(const SkinKey &k) const {
    return bones == k.bones && model == k.model && count == k.count;
  }
};
struct SkinHash {
  size_t operator()(const SkinKey &k) const {
    return uintptr_t(k.bones) ^ (uintptr_t(k.model) << 1) ^ unsigned(k.count);
  }
};
struct PoseProbe {
  unsigned frame = ~0u, hash = 0, unique = 0, seen = 0, tick = 0;
};
static std::unordered_map<SkinKey, PoseProbe, SkinHash> poses;
static SkinKey currentSkin{};
static bool uploadingSkin;
static const float *drawingModel;
static const float *drawingView;
static char *modelOwner;
static const float *modelView;
static float cameraNative[16], cameraRendered[16];
static unsigned cameraFrame = ~0u;
// Native tutebook state: set by 63D5D0, cleared by the book close path.
static bool manualBookActive() {
  bool active=game<int>(0x520880)!=0 && game<uintptr_t>(0x520468)!=0;
  static bool last;
  if(diagnostics && active!=last) {
    log("Manual camera isolation active=%d tick=%u",active,game<unsigned>(0x1de47f4));
    last=active;
  }
  return active;
}
static bool drawArgumentsFiltered;
static int worldShaderDepth;
static int worldMaterialDepth, worldParticleDepth;
static const void *materialDescriptor;
static int shadowCasterDepth;
static bool visibleSceneView(const float *view) {
  int w=int(outputWidth)+game<int>(0x1de8d58), h=int(outputHeight)+game<int>(0x1de8d5c);
  bool viewport=(actualViewport.Width==outputWidth && actualViewport.Height==outputHeight) ||
    (int(actualViewport.Width)==w && int(actualViewport.Height)==h);
  return viewport && view && (!memcmp(view,reinterpret_cast<void*>(gameBase+0x9a7900),64) ||
    !memcmp(view,reinterpret_cast<void*>(gameBase+0x9a9c00),64));
}
static bool filterDrawArguments(const float *, const float *, float *, float *, unsigned = 0);
static float *smoothPacked(const SkinKey &, const float *, unsigned);
using ShaderDrawFn = unsigned(__thiscall *)(void *, const float *,
                                            const float *, unsigned, unsigned,
                                            const void *, const void *);
static ShaderDrawFn shaderDraw;
static unsigned __fastcall shaderDrawHook(void *obj, void *, const float *model,
                                          const float *view, unsigned mode,
                                          unsigned a, const void *b,
                                          const void *c) {
  auto oldModel = drawingModel, oldView = drawingView;
  drawingModel = model;
  drawingView = view;
  auto oldFiltered = drawArgumentsFiltered;
  auto oldDescriptor=materialDescriptor;
  bool material=visibleSceneView(view);
  if(material) { ++worldMaterialDepth; materialDescriptor=obj; }
  float renderedWorld[16], renderedView[16];
  drawArgumentsFiltered =
      interpolateMotion &&
      filterDrawArguments(model, view, renderedWorld, renderedView,mode);
  if (diagnostics && modelOwner &&
      (!strcmp(modelOwner, "back2") || !strcmp(modelOwner, "back5"))) {
    static unsigned tracedSky;
    if (tracedSky++ < 6) {
      log("Sky probe model=%s mode=%u viewport=%ux%u cameraFrame=%u frame=%u",
          modelOwner, mode, actualViewport.Width, actualViewport.Height,
          cameraFrame, frames);
      for (unsigned row = 0; row < 4; ++row)
        log("Sky matrices row=%u world=%g,%g,%g,%g view=%g,%g,%g,%g main=%g,%g,%g,%g",
            row, model[row*4], model[row*4+1], model[row*4+2], model[row*4+3],
            view[row*4], view[row*4+1], view[row*4+2], view[row*4+3],
            game<float>(0x9a7900+row*16), game<float>(0x9a7904+row*16),
            game<float>(0x9a7908+row*16), game<float>(0x9a790c+row*16));
    }
  }
  if (diagnostics && modelOwner) {
    static std::unordered_map<uintptr_t, bool> reported;
    uintptr_t key = uintptr_t(modelOwner) ^ (uintptr_t(mode) << 24);
    if (!reported[key] && reported.size() < 500) {
      log("Draw model=%.32s mode=%u filteredArgs=%d vertexFrame=%u "
          "geometryFrames=%u rowsPerFrame=%u frameCount=%u",
          modelOwner, mode, drawArgumentsFiltered,
          *reinterpret_cast<unsigned *>(modelOwner + 0x2cc),
          *reinterpret_cast<unsigned *>(modelOwner + 0x1c0),
          *reinterpret_cast<unsigned *>(modelOwner + 0x1c4),
          *reinterpret_cast<unsigned *>(modelOwner + 0x1c8));
      reported[key] = true;
    }
  }
  int savedWidth = game<int>(0x508b14), savedHeight = game<int>(0x508b18);
  if (drawArgumentsFiltered) {
    game<int>(0x508b14) = actualViewport.Width;
    game<int>(0x508b18) = actualViewport.Height;
    ++worldShaderDepth;
  }
  auto r =
      shaderDraw(obj, drawArgumentsFiltered ? renderedWorld : model,
                 drawArgumentsFiltered ? renderedView : view, mode, a, b, c);
  if (drawArgumentsFiltered)
    --worldShaderDepth;
  game<int>(0x508b14) = savedWidth;
  game<int>(0x508b18) = savedHeight;
  drawArgumentsFiltered = oldFiltered;
  drawingModel = oldModel;
  drawingView = oldView;
  if(material) --worldMaterialDepth;
  materialDescriptor=oldDescriptor;
  return r;
}
struct RenderPose {
  std::vector<float> previous, current, result;
  unsigned tick = ~0u, frame = ~0u, lastSeen = 0, inputHash = 0, outputHash = 0,
           inputs = 0, outputs = 0, samples = 0, rejected = 0, bypasses = 0;
  double stamp = 0;
};
static std::unordered_map<SkinKey, RenderPose, SkinHash> renderPoses;
using ModelDrawFn = unsigned(__thiscall *)(void *, const float *, const float *,
                                           unsigned, unsigned, const void *,
                                           const void *);
static ModelDrawFn modelDraw, modelDrawAlt, modelDrawThird;
static unsigned __fastcall modelDrawHook(void *obj, void *, const float *parent,
                                         const float *view, unsigned a,
                                         unsigned b, const void *c,
                                         const void *e) {
  auto old = modelOwner;
  auto oldView = modelView;
  bool caster=shadowCasterDepth ||
      uintptr_t(__builtin_return_address(0))-gameBase==0x369853;
  if(caster) ++shadowCasterDepth;
  modelView = view;
  modelOwner = static_cast<char *>(obj);
  auto r = modelDraw(obj, parent, view, a, b, c, e);
  if(caster) --shadowCasterDepth;
  modelOwner = old;
  modelView = oldView;
  return r;
}
static unsigned __fastcall
modelDrawAltHook(void *obj, void *, const float *parent, const float *view,
                 unsigned a, unsigned b, const void *c, const void *e) {
  auto old = modelOwner;
  auto oldView = modelView;
  modelView = view;
  modelOwner = static_cast<char *>(obj);
  auto r = modelDrawAlt(obj, parent, view, a, b, c, e);
  modelOwner = old;
  modelView = oldView;
  return r;
}
static unsigned __fastcall
modelDrawThirdHook(void *obj, void *, const float *parent, const float *view,
                   unsigned a, unsigned b, const void *c, const void *e) {
  auto old = modelOwner;
  auto oldView = modelView;
  modelView = view;
  modelOwner = static_cast<char *>(obj);
  auto r = modelDrawThird(obj, parent, view, a, b, c, e);
  modelOwner = old;
  modelView = oldView;
  return r;
}
struct SkinBody {
  SkinKey key{};
  unsigned frame = ~0u, sequence = 0;
  float view[16]{};
  bool hasView = false;
  std::vector<float> native, submitted;
  std::vector<const char*> boneOwners;
};
static std::unordered_map<const char *, SkinBody> skinBodies;
static void unpackAffine(const float *p, float *m) {
  std::fill(m, m + 16, 0.f);
  m[15] = 1;
  for (int row = 0; row < 3; ++row)
    for (int col = 0; col < 4; ++col)
      m[col * 4 + row] = p[row * 4 + col];
}
static void multiplyMatrix(const float *a, const float *b, float *out) {
  float r[16]{};
  for (int row = 0; row < 4; ++row)
    for (int col = 0; col < 4; ++col)
      for (int k = 0; k < 4; ++k)
        r[row * 4 + col] += a[row * 4 + k] * b[k * 4 + col];
  memcpy(out, r, 64);
}
static bool inverseAffine(const float *m, float *out) {
  float det = m[0] * (m[5] * m[10] - m[6] * m[9]) -
              m[1] * (m[4] * m[10] - m[6] * m[8]) +
              m[2] * (m[4] * m[9] - m[5] * m[8]);
  if (!std::isfinite(det) || std::abs(det) < 1e-8)
    return false;
  float r[16]{};
  r[15] = 1;
  r[0] = (m[5] * m[10] - m[6] * m[9]) / det;
  r[1] = (m[2] * m[9] - m[1] * m[10]) / det;
  r[2] = (m[1] * m[6] - m[2] * m[5]) / det;
  r[4] = (m[6] * m[8] - m[4] * m[10]) / det;
  r[5] = (m[0] * m[10] - m[2] * m[8]) / det;
  r[6] = (m[2] * m[4] - m[0] * m[6]) / det;
  r[8] = (m[4] * m[9] - m[5] * m[8]) / det;
  r[9] = (m[1] * m[8] - m[0] * m[9]) / det;
  r[10] = (m[0] * m[5] - m[1] * m[4]) / det;
  for (int col = 0; col < 3; ++col)
    r[12 + col] = -(m[12] * r[col] + m[13] * r[4 + col] + m[14] * r[8 + col]);
  memcpy(out, r, 64);
  return true;
}
static bool smoothAttachment(const float *packed, float *output) {
  if (!modelOwner ||
      drawingModel != reinterpret_cast<const float *>(modelOwner + 0x24c))
    return false;
  const char *ancestors[64];
  unsigned ancestorCount = 0;
  auto node = modelOwner;
  while (node && ancestorCount < 64) {
    if(std::find(ancestors,ancestors+ancestorCount,node)!=ancestors+ancestorCount ||
        !readableRange(node+0x74c,sizeof(char*))) return false;
    ancestors[ancestorCount++] = node;
    node = *reinterpret_cast<char **>(node + 0x74c);
  }
  if(node) return false; // Unterminated hierarchy exceeded the bounded walk.
  for (unsigned ancestor = 1; ancestor < ancestorCount; ++ancestor) {
    const SkinBody *selected = nullptr;
    const char *bodyOwner = nullptr;
    int boneIndex = -1;
    unsigned candidates = 0;
    for (const auto &entry : skinBodies) {
      const auto &body = entry.second;
      if (body.frame != frames || !body.hasView || !modelView ||
          memcmp(body.view, modelView, 64) ||
          body.native.size() != unsigned(body.key.count * 12) ||
          body.submitted.size() != body.native.size() ||
          body.boneOwners.size()!=unsigned(body.key.count))
        continue;
      for (int index = 0; index < body.key.count; ++index) {
        if (body.boneOwners[index] != ancestors[ancestor])
          continue;
        selected = &body;
        bodyOwner = entry.first;
        boneIndex = index;
        ++candidates;
      }
    }
    if (!candidates)
      continue;
    if (candidates != 1) {
      static unsigned reports;
      if (diagnostics && reports++ < 10)
        log("Ambiguous attachment model=%.32s candidates=%u", modelOwner,
            candidates);
      return false;
    }
    float boneNative[16], boneSubmitted[16], inverse[16], child[16], local[16],
        result[16];
    unpackAffine(selected->native.data() + boneIndex * 12, boneNative);
    unpackAffine(selected->submitted.data() + boneIndex * 12, boneSubmitted);
    unpackAffine(packed, child);
    if (!inverseAffine(boneNative, inverse))
      return false;
    multiplyMatrix(child, inverse, local);
    if (diagnostics &&
        (strstr(modelOwner, "face_") || strstr(modelOwner, "Layer2"))) {
      struct LocalProbe {
        unsigned first = 0, last = ~0u;
      };
      static std::unordered_map<const char *, LocalProbe> probes;
      auto &probe = probes[modelOwner];
      if (!probe.first)
        probe.first = frames;
      if (frames - probe.first < 180 && probe.last != frames) {
        probe.last = frames;
        log("Attachment local frame=%u tick=%u model=%.32s vertex=%u "
            "bone=%d "
            "local=%.7g,%.7g,%.7g,%.7g,%.7g,%.7g,%.7g,%.7g,%.7g,%.7g,%.7g,%.7g",
            frames, game<unsigned>(0x1de47f4), modelOwner,
            *reinterpret_cast<unsigned *>(modelOwner + 0x2cc), boneIndex,
            local[0], local[1], local[2], local[4], local[5], local[6],
            local[8], local[9], local[10], local[12], local[13], local[14]);
      }
    }
    // The child may animate locally as well as following its parent. Keep that
    // motion on the same previous/current interval as the submitted bone.
    float packedLocal[12];
    for (int row = 0; row < 3; ++row)
      for (int col = 0; col < 4; ++col)
        packedLocal[row * 4 + col] = local[col * 4 + row];
    auto renderedLocal = smoothPacked(
        {selected->key.bones, reinterpret_cast<const float *>(modelOwner), -1},
        packedLocal, 3);
    if (renderedLocal)
      unpackAffine(renderedLocal, local);
    multiplyMatrix(local, boneSubmitted, result);
    for (int row = 0; row < 3; ++row)
      for (int col = 0; col < 4; ++col)
        output[row * 4 + col] = result[col * 4 + row];
    static std::unordered_map<const char *, bool> reported;
    if (diagnostics && !reported[modelOwner]) {
      log("Actual-palette attachment model=%.32s bone=%d body=%p sequence=%u",
          modelOwner, boneIndex, bodyOwner, selected->sequence);
      reported[modelOwner] = true;
    }
    return true;
  }
  return false;
}
static unsigned matrixHash(const float *m, unsigned n) {
  unsigned h = 2166136261u;
  for (unsigned i = 0; i < n; ++i) {
    uint32_t bits;
    memcpy(&bits, m + i, 4);
    h = (h ^ bits) * 16777619u;
  }
  return h;
}
static float determinant3(const float *m) {
  return m[0] * (m[5] * m[10] - m[6] * m[9]) -
         m[1] * (m[4] * m[10] - m[6] * m[8]) +
         m[2] * (m[4] * m[9] - m[5] * m[8]);
}
static bool interpolateRenderMatrix(const float *a, const float *b, float alpha,
                                    float *out) {
  bool reflectedA = determinant3(a) < 0, reflectedB = determinant3(b) < 0;
  if (reflectedA != reflectedB)
    return false;
  if (!reflectedA)
    return gurumin::interpolateAffine(a, b, alpha, out);
  float first[16], second[16];
  memcpy(first, a, 64);
  memcpy(second, b, 64);
  // Falcom's camera basis includes a fixed handedness reflection. Factor it
  // out for quaternion rotation interpolation, then restore it exactly.
  for (int i = 0; i < 3; ++i) {
    first[i] = -first[i];
    second[i] = -second[i];
  }
  if (!gurumin::interpolateAffine(first, second, alpha, out))
    return false;
  for (int i = 0; i < 3; ++i)
    out[i] = -out[i];
  return true;
}
static float *smoothPacked(const SkinKey &key, const float *data, unsigned n) {
  auto &p = renderPoses[key];
  unsigned tick = game<unsigned>(0x1de47f4), h = matrixHash(data, n * 4);
  double time = renderTime();
  bool reset = p.current.size() != n * 4 || frames - p.lastSeen > 10 ||
               tick - p.tick > 2;
  if (reset) {
    p.current.assign(data, data + n * 4);
    p.previous = p.current;
    p.result = p.current;
    p.tick = tick;
    p.stamp = sharedTickStamp;
    p.frame = ~0u;
  } else if (tick != p.tick) {
    p.previous = p.current;
    p.current.assign(data, data + n * 4);
    p.tick = tick;
    p.stamp = sharedTickStamp;
    p.frame = ~0u;
  }
  if (p.frame == frames && h != p.inputHash) {
    if (p.bypasses++ < 3)
      log("Different render pass key=%p/%p frame=%u", key.bones, key.model,
          frames);
    return nullptr; // Separate passes must not share differing transforms.
  }
  p.lastSeen = frames;
  if (p.frame != frames) {
    float alpha = std::clamp(float((time - p.stamp) * 30.), 0.f, 1.f);
    p.result = p.current;
    for (unsigned bone = 0; bone < n / 3; ++bone) {
      // Receiver geometry retains its native stationary world matrix. Do not
      // introduce quaternion roundoff into the identical visible geometry.
      if (alpha == 1.f || !memcmp(p.previous.data()+bone*12,
                                  p.current.data()+bone*12,48))
        continue;
      float a[16]{}, b[16]{}, out[16];
      a[15] = b[15] = 1;
      for (unsigned row = 0; row < 3; ++row)
        for (unsigned col = 0; col < 4; ++col) {
          a[col * 4 + row] = p.previous[bone * 12 + row * 4 + col];
          b[col * 4 + row] = p.current[bone * 12 + row * 4 + col];
        }
      float dx = a[12] - b[12], dy = a[13] - b[13], dz = a[14] - b[14];
      if (dx * dx + dy * dy + dz * dz < 250000.f &&
          interpolateRenderMatrix(a, b, alpha, out))
        for (unsigned row = 0; row < 3; ++row)
          for (unsigned col = 0; col < 4; ++col)
            p.result[bone * 12 + row * 4 + col] = out[col * 4 + row];
      else
        ++p.rejected;
    }
    if (h != p.inputHash)
      ++p.inputs;
    p.inputHash = h;
    unsigned outHash = matrixHash(p.result.data(), n * 4);
    if (outHash != p.outputHash)
      ++p.outputs;
    p.outputHash = outHash;
    ++p.samples;
    p.frame = frames;
    if (diagnostics && p.samples % 600 == 0)
      log("Smooth key=%p/%p samples=%u nativeChanges=%u renderedChanges=%u "
          "rejectedMatrices=%u",
          key.bones, key.model, p.samples, p.inputs, p.outputs, p.rejected);
  }
  return p.result.data();
}
static bool sameTranspose(const float *packed, const float *source,
                          unsigned rows) {
  if (!source)
    return false;
  for (unsigned row = 0; row < rows; ++row)
    for (unsigned col = 0; col < 4; ++col)
      if (packed[row * 4 + col] != source[col * 4 + row])
        return false;
  return true;
}
static bool factorPerspectiveView(const float *vp, const float *p, float *view) {
  for(unsigned i=0;i<16;++i)
    if(!std::isfinite(vp[i]) || !std::isfinite(p[i])) return false;
  if(p[0]<=0 || p[5]<=0 || std::abs(p[14])<.001f ||
     p[11]!=1.f || p[15]!=0.f) return false;
  for(int row=0;row<4;++row) {
    view[row*4]=vp[row*4]/p[0];
    view[row*4+1]=vp[row*4+1]/p[5];
    view[row*4+2]=vp[row*4+3];
    view[row*4+3]=(vp[row*4+2]-p[10]*vp[row*4+3])/p[14];
  }
  if(std::abs(view[3])>.001f || std::abs(view[7])>.001f ||
     std::abs(view[11])>.001f || std::abs(view[15]-1)>.001f) return false;
  view[3]=view[7]=view[11]=0; view[15]=1;
  float check[16];
  if(!interpolateRenderMatrix(view,view,0,check)) return false;
  for(int row=0;row<4;++row) {
    float reconstructed[4]={view[row*4]*p[0],view[row*4+1]*p[5],
      view[row*4+2]*p[10]+view[row*4+3]*p[14],view[row*4+2]};
    for(int col=0;col<4;++col)
      if(std::abs(reconstructed[col]-vp[row*4+col])>
         .001f*std::max(1.f,std::abs(vp[row*4+col]))) return false;
  }
  return true;
}
static bool smoothCamera(const float *packed, float *result) {
  if(manualBookActive()) {
    cameraFrame=~0u;
    renderPoses.erase({reinterpret_cast<const float*>(gameBase+0x9ac940),
        reinterpret_cast<const float*>(gameBase+0x9a7900),1});
    return false;
  }
  if (!drawingView || !sameTranspose(packed, drawingView, 4))
    return false;
  auto main = reinterpret_cast<const float *>(gameBase + 0x9a7900);
  auto p = reinterpret_cast<const float *>(gameBase + 0x9ac940);
  auto alternate = reinterpret_cast<const float *>(gameBase + 0x9a9c00);
  bool alternatePass = !memcmp(drawingView,alternate,64) &&
                       memcmp(drawingView,main,64);
  auto passProjection = alternatePass
    ? reinterpret_cast<const float *>(gameBase+0x975d18) : p;
  if (p[0] <= 0 || p[5] <= 0 || std::abs(p[14]) < .001f || p[11] != 1.f ||
      p[15] != 0.f)
    return false;
  float view[16];
  if(!factorPerspectiveView(drawingView,passProjection,view)) return false;
  if(alternatePass) {
    float mainView[16];
    if(!factorPerspectiveView(main,p,mainView)) return false;
    for(unsigned i=0;i<16;++i)
      if(std::abs(view[i]-mainView[i]) > (i<12?.005f:.05f)) {
        static unsigned reported;
        if(diagnostics && reported++<6)
          log("Alternate camera differs from main at %u: %g vs %g; native retained",i,view[i],mainView[i]);
        return false;
      }
    memcpy(view,mainView,64);
  }
  // Only accept the factorization when it recovers a rigid affine view.
  if (std::abs(view[3]) > .001f || std::abs(view[7]) > .001f ||
      std::abs(view[11]) > .001f || std::abs(view[15] - 1) > .001f)
    return false;
  view[3] = view[7] = view[11] = 0;
  view[15] = 1;
  for (int row = 0; row < 3; ++row) {
    float length = 0;
    for (int col = 0; col < 3; ++col)
      length += view[row * 4 + col] * view[row * 4 + col];
    if (std::abs(length - 1.f) > .005f)
      return false;
  }
  float check[16];
  if (!interpolateRenderMatrix(view, view, 0, check))
    return false;
  // Interpolate physical eye position, not view-space translation (-eye*R).
  // A small authored turn far from the map origin must not trip the shared
  // 500-unit teleport guard or move a stationary eye along an artificial arc.
  float cameraPose[16];
  if(!inverseAffine(view,cameraPose)) return false;
  float affine[12];
  for (int row = 0; row < 3; ++row)
    for (int col = 0; col < 4; ++col)
      affine[row * 4 + col] = cameraPose[col * 4 + row];
  auto smooth = smoothPacked({p, main, 1}, affine, 3);
  if (!smooth)
    return false;
  float renderedPose[16],filtered[16];
  unpackAffine(smooth,renderedPose);
  if(!inverseAffine(renderedPose,filtered)) return false;
  // Compute once: sparse and generic products round differently on x86 x87.
  // Coplanar floor and shadow receiver must use byte-identical scene VP.
  float renderedVP[16];
  auto compose=[&](const float *projection,float *out) {
    for(unsigned row=0;row<4;++row) {
      out[row*4]=filtered[row*4]*projection[0];
      out[row*4+1]=filtered[row*4+1]*projection[5];
      out[row*4+2]=filtered[row*4+2]*projection[10]+filtered[row*4+3]*projection[14];
      out[row*4+3]=filtered[row*4+2];
    }
  };
  compose(passProjection,renderedVP);
  for(unsigned row=0;row<4;++row)
    for(unsigned col=0;col<4;++col)
      result[row*4+col]=renderedVP[col*4+row];
  memcpy(cameraNative, alternatePass?main:drawingView, 64);
  if(alternatePass) compose(p,cameraRendered);
  else memcpy(cameraRendered,renderedVP,64);
  cameraFrame = frames;
  if(alternatePass) {
    static bool noted;
    if(!noted) { log("Alternate sky projection shares validated main camera history"); noted=true; }
  }
  static bool logged;
  if (!logged) {
    log("Main camera validated: projectionX=%g projectionY=%g verticalFOV=%.3f "
        "horizontalFOV=%.3f ratio=%g",
        p[0], p[5], 2 * atan(1. / p[5]) * 180 / 3.141592653589793,
        2 * atan(1. / p[0]) * 180 / 3.141592653589793, p[0] / p[5]);
    logged = true;
  }
  return true;
}
static bool filterDrawArguments(const float *world, const float *view,
                                float *outWorld, float *outView,unsigned mode) {
  if (!world || !view)
    return false;
  int sceneWidth = int(outputWidth) + game<int>(0x1de8d58);
  int sceneHeight = int(outputHeight) + game<int>(0x1de8d5c);
  bool sceneViewport = (actualViewport.Width == outputWidth &&
                        actualViewport.Height == outputHeight) ||
                       (int(actualViewport.Width) == sceneWidth &&
                        int(actualViewport.Height) == sceneHeight);
  bool caster=shadowCasterDepth && mode==16 && !manualBookActive() &&
      actualViewport.Width==actualViewport.Height &&
      (actualViewport.Width==256 || actualViewport.Width==512 ||
       actualViewport.Width==1024 || actualViewport.Width==2048);
  if(caster) {
    // Only the verified native shadow-model subtree. Its light camera stays
    // native, while rigid children follow the actual submitted skin palette.
    memcpy(outView,view,64);
  } else {
    if(!sceneViewport) return false;
    float packedView[16], filteredView[16];
    for (int row = 0; row < 4; ++row)
      for (int col = 0; col < 4; ++col)
        packedView[row * 4 + col] = view[col * 4 + row];
    if (!smoothCamera(packedView, filteredView)) return false;
    for (int row = 0; row < 4; ++row)
      for (int col = 0; col < 4; ++col)
        outView[row * 4 + col] = filteredView[col * 4 + row];
  }
  float packedWorld[12], attachment[12];
  for (int row = 0; row < 3; ++row)
    for (int col = 0; col < 4; ++col)
      packedWorld[row * 4 + col] = world[col * 4 + row];
  if (smoothAttachment(packedWorld, attachment))
    unpackAffine(attachment, outWorld);
  else {
    auto rendered = smoothPacked({nullptr, world, 1}, packedWorld, 3);
    if (rendered)
      unpackAffine(rendered, outWorld);
    else
      memcpy(outWorld, world, 64);
  }
  return true;
}
static void rememberSubmittedPalette(const float *native,
                                     const float *submitted, unsigned n) {
  if (!modelOwner ||
      currentSkin.model != reinterpret_cast<const float *>(modelOwner + 0x24c))
    return;
  static unsigned sequence;
  auto &body = skinBodies[modelOwner];
  body.key = currentSkin;
  body.boneOwners.clear();
  // The submitted native palette owns this mapping now. Never chase its
  // controller pointer later: equipment replacement may free it in-frame.
  uintptr_t bones=uintptr_t(currentSkin.bones);
  if(currentSkin.count>0 && currentSkin.count<=26 && bones>=0x4618u) {
    auto owners=reinterpret_cast<const char*const*>(bones-0x4618u+0x1108u);
    if(readableRange(owners,std::size_t(currentSkin.count)*sizeof(char*)))
      body.boneOwners.assign(owners,owners+currentSkin.count);
  }
  body.frame = frames;
  body.sequence = ++sequence;
  body.native.assign(native, native + n * 4);
  body.submitted.assign(submitted, submitted + n * 4);
  body.hasView = modelView != nullptr;
  if (modelView)
    memcpy(body.view, modelView, 64);
}
// Feed the shadow receiver the exact rendered scene VP before CPU culling
// and its dependent c0 and world*VP c12 shader uploads.
static int shadowReceiverDepth;
static unsigned shadowEntries, shadowMatched;
using ShadowReceiverFn = unsigned(__thiscall *)(void *, uintptr_t,
                                                const float *, uintptr_t,
                                                uintptr_t, uintptr_t, uintptr_t,
                                                uintptr_t, uintptr_t, uintptr_t,
                                                uintptr_t, uintptr_t, uintptr_t,
                                                uintptr_t, uintptr_t);
static ShadowReceiverFn shadowReceiver;
static unsigned __fastcall
shadowReceiverHook(void *obj, void *, uintptr_t a, const float *view,
                   uintptr_t c, uintptr_t e, uintptr_t f, uintptr_t g,
                   uintptr_t h, uintptr_t i, uintptr_t j, uintptr_t k,
                   uintptr_t l, uintptr_t m, uintptr_t n, uintptr_t o) {
  uintptr_t caller = uintptr_t(__builtin_return_address(0)) - gameBase;
  bool scoped = shadowReceiverDepth || caller == 0x3526bc;
  if (scoped) {
    ++shadowReceiverDepth;
    ++shadowEntries;
    if (interpolateMotion && !manualBookActive() && cameraFrame == frames && view &&
        (!memcmp(view, cameraNative, 64) || view == cameraRendered)) {
      view = cameraRendered;
      ++shadowMatched;
    }
    if (diagnostics && shadowEntries < 20)
      log("Shadow receiver camera frame=%u entries=%u matched=%u caller=%lx",
          frames, shadowEntries, shadowMatched, (unsigned long)caller);
  }
  auto result =
      shadowReceiver(obj, a, view, c, e, f, g, h, i, j, k, l, m, n, o);
  if (scoped)
    --shadowReceiverDepth;
  return result;
}
using SkinFn = void(WINAPI *)(const float *, int, const float *, void *);
static SkinFn uploadSkin;
static void WINAPI skinHook(const float *bones, int count, const float *model,
                            void *extra) {
  auto saved = currentSkin;
  bool active = uploadingSkin;
  currentSkin = {bones, model, count};
  uploadingSkin = true;
  uploadSkin(bones, count, model, extra);
  uploadingSkin = active;
  currentSkin = saved;
}
static float uiWidth() {
  return std::min(float(outputWidth), float(outputHeight) * 4.f / 3.f);
}
static float rhythmDelta, rhythmBobDelta, spriteXDelta, spriteYDelta;
static uintptr_t activeSpriteCaller;
static void updateRhythm() {
  rhythmDelta = rhythmBobDelta = 0;
  if (!smoothRhythm)
    return;
  int id = game<int>(0x508784);
  if (id < 0 || id >= 255)
    return;
  int tempo = game<int>(0xbab12c + id * 48);
  if (tempo <= 0)
    return;
  int period = 2646000 / tempo;
  if (period <= 0)
    return;
  double sample = std::numeric_limits<double>::quiet_NaN();
  if (game<int>(0x1e2693c) && game<int>(0x613960)) {
    using AudioPositionFn = int(__cdecl *)(int);
    auto reader = game<AudioPositionFn>(0x5094dc);
    if (reader)
      sample = reader(game<int>(0x7493f4)) + 2500 + game<short>(0x5e3390);
  } else {
    sample = double(DWORD(GetTickCount() - game<DWORD>(0x5e33b0))) *
             game<float>(0x44dee8);
    int end = game<int>(0xbab124 + id * 48);
    if (sample >= end)
      sample += game<int>(0xbab120 + id * 48) - end;
  }
  double audioPhase = (sample - game<int>(0xbab128 + id * 48)) * 15. / period;
  int native = game<int>(0x5e160c);
  static gurumin::RhythmClock clock;
  double now = nowSeconds();
  if (!clock.update(id, now, 44100. * 15. / period, audioPhase))
    return;
  rhythmDelta = float(std::remainder(clock.phase - native, 480.));
  rhythmBobDelta = float(gurumin::rhythmBounce(clock.phase) -
                         gurumin::nativeRhythmBounce(native));
  if (diagnostics && frames % 1800 < 180)
    log("Rhythm clock frame=%u t=%.6f audio=%.5f predicted=%.5f "
        "innovation=%.5f reason=%d native=%d delta=%.5f",
        frames, now, audioPhase, clock.phase, clock.innovation,
        int(clock.reason), native, rhythmDelta);
}
using SpriteFn = unsigned(__thiscall *)(void *, int, int, int, int, float,
                                        float, float, float, float, float,
                                        float, float, int);
static SpriteFn sprite;
static void *titlePatternResource;
static char titlePatternName[64];
static int titlePatternDepth;
static unsigned __fastcall spriteHook(void *texture, void *, int x0, int y0,
                                      int x1, int y1, float u0, float v0,
                                      float u1, float v1, float z, float alpha,
                                      float angle, float other, int mode) {
  uintptr_t caller = uintptr_t(__builtin_return_address(0)) - gameBase;
  if(caller==0x22d463 || caller==0x22d53d) {
    uintptr_t offset=uintptr_t(texture)-(gameBase+0x198f6a0);
    if(offset<1600*0x38u && offset%0x38u==0) {
      titlePatternResource=texture;
      memcpy(titlePatternName,reinterpret_cast<void*>(gameBase+0x128c7d0+(offset/0x38u)*0x40),64);
    }
  }
  bool pattern=texture==titlePatternResource && texture && x0==0 && y0==0 &&
    x1==640 && y1==480 && mode==0 && std::abs(u1-u0-5)<.001f &&
    std::abs(v1-v0-3.75f)<.001f;
  if(pattern) {
    uintptr_t offset=uintptr_t(texture)-(gameBase+0x198f6a0);
    pattern=!memcmp(titlePatternName,reinterpret_cast<void*>(gameBase+0x128c7d0+(offset/0x38u)*0x40),64);
  }
  float savedX = spriteXDelta, savedY = spriteYDelta;
  auto savedCaller = activeSpriteCaller;
  activeSpriteCaller = caller;
  if (hudDepth &&
      (caller == 0x1d0362 || caller == 0x1d0441 || caller == 0x1d0543))
    spriteXDelta = -2.f * rhythmDelta * uiWidth() / 640.f;
  if (hudDepth &&
      (caller == 0x1d03cc || caller == 0x1d04a8 || caller == 0x1d05ad))
    spriteXDelta = 2.f * rhythmDelta * uiWidth() / 640.f;
  if (hudDepth && caller == 0x1d0de9 && x0 == 299)
    spriteYDelta = rhythmBobDelta * outputHeight / 480.f;
  if(pattern) ++titlePatternDepth;
  auto r = sprite(texture, x0, y0, x1, y1, u0, v0, u1, v1, z, alpha, angle,
                  other, mode);
  if(pattern) --titlePatternDepth;
  activeSpriteCaller = savedCaller;
  spriteXDelta = savedX;
  spriteYDelta = savedY;
  return r;
}
// Native color rectangle helper; fullscreen fades retain their native alpha.
using SolidRectFn = unsigned(__cdecl *)(int, int, int, int, unsigned, float, float);
static SolidRectFn solidRect;
static int fullscreenSolidDepth;
static unsigned __cdecl solidRectHook(int x0, int y0, int x1, int y1,
                                      unsigned color, float z, float alpha) {
  auto caller=uintptr_t(__builtin_return_address(0))-gameBase;
  // These are the native cinematic bars and actor-script fades, rather than
  // HUD panels. Keep their vertical bounds and animated opacity untouched.
  bool bars = (caller==0x1d2e71 || caller==0x1d2e9b ||
               caller==0x1db546 || caller==0x1db570 ||
               caller==0x32a391 || caller==0x32a3be) && color==0 &&
              ((y0==0 && y1==34) || (y0==446 && y1==480));
  bool fade = (caller==0x1df06a || caller==0x32a33e || caller==0x32a418) &&
              y0==0 && y1==480;
  bool full = x0==0 && x1==640 && (bars || fade);
  if (full) ++fullscreenSolidDepth;
  auto result = solidRect(x0, y0, x1, y1, color, z, alpha);
  if (full) --fullscreenSolidDepth;
  return result;
}
static void introWings() {
  auto d=game<IDirect3DDevice9*>(0x1da3540);
  if(!d || !drawUP || outputWidth<=uiWidth() ||
     actualViewport.Width!=outputWidth || actualViewport.Height!=outputHeight)
    return;
  IDirect3DStateBlock9 *saved=nullptr;
  if(FAILED(d->CreateStateBlock(D3DSBT_ALL,&saved))) return;
  bool ok=SUCCEEDED(saved->Capture());
  auto state=[&](D3DRENDERSTATETYPE s,DWORD value) {
    if(ok) ok=SUCCEEDED(d->SetRenderState(s,value));
  };
  if(ok) ok=SUCCEEDED(d->SetVertexShader(nullptr));
  if(ok) ok=SUCCEEDED(d->SetPixelShader(nullptr));
  if(ok) ok=SUCCEEDED(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE));
  if(ok) ok=SUCCEEDED(d->SetTexture(0,nullptr));
  if(ok) ok=SUCCEEDED(d->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1));
  if(ok) ok=SUCCEEDED(d->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_DIFFUSE));
  if(ok) ok=SUCCEEDED(d->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1));
  if(ok) ok=SUCCEEDED(d->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_DIFFUSE));
  if(ok) ok=SUCCEEDED(d->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE));
  state(D3DRS_ZENABLE,FALSE); state(D3DRS_ZWRITEENABLE,FALSE);
  state(D3DRS_ALPHABLENDENABLE,FALSE); state(D3DRS_ALPHATESTENABLE,FALSE);
  state(D3DRS_CULLMODE,D3DCULL_NONE); state(D3DRS_FOGENABLE,FALSE);
  state(D3DRS_SCISSORTESTENABLE,FALSE); state(D3DRS_COLORWRITEENABLE,15);
  struct Vertex { float x,y,z,w; DWORD color; };
  float gap=(outputWidth-uiWidth())/2;
  if(ok) {
    for(unsigned side=0;side<2;++side) {
      float left=side?outputWidth-gap:0, right=side?outputWidth:gap;
      Vertex quad[]={{left-.5f,-.5f,0,1,0xff000000},
        {right-.5f,-.5f,0,1,0xff000000},
        {left-.5f,outputHeight-.5f,0,1,0xff000000},
        {right-.5f,outputHeight-.5f,0,1,0xff000000}};
      drawUP(d,D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex));
    }
    static bool noted;
    if(!noted) { log("Falcom opening wings covered after character submission"); noted=true; }
  }
  saved->Apply(); saved->Release();
}
using FadeFn=unsigned(__cdecl*)();
static FadeFn nativeFade;
static unsigned __cdecl fadeHook() {
  if(uintptr_t(__builtin_return_address(0))-gameBase==0x1f3866)
    introWings();
  return nativeFade();
}
using HudFn = unsigned(__cdecl *)();
static HudFn upperHud, lowerHud;
static unsigned __cdecl upperHook() {
  ++hudDepth;
  hudSeen = true;
  lastHudFrame = frames;
  updateRhythm();
  auto r = upperHud();
  --hudDepth;
  return r;
}
static unsigned __cdecl lowerHook() {
  ++hudDepth;
  hudSeen = true;
  lastHudFrame = frames;
  auto r = lowerHud();
  --hudDepth;
  return r;
}
using MarkerFn = unsigned(__cdecl *)(void *);
using BubbleFn = unsigned(__cdecl *)(void *, int, int);
static MarkerFn marker;
static BubbleFn bubble;
static unsigned __cdecl markerHook(void *m) {
  ++worldDepth;
  auto r = marker(m);
  --worldDepth;
  return r;
}
static bool bubbleScope, bubbleProjected;
static float bubbleX, bubbleY, bubbleSavedX, bubbleSavedY;
using BubbleAnchorFn=float*(__thiscall*)(const float*,float*);
using BubblePanelFn=unsigned(__thiscall*)(void*,int,int,int,int,float,float);
static BubbleAnchorFn bubbleAnchor;
static BubblePanelFn bubblePanel;
static float *__fastcall bubbleAnchorHook(const float *matrix,void*,float *out) {
  auto result=bubbleAnchor(matrix,out);
  uintptr_t caller=uintptr_t(__builtin_return_address(0))-gameBase;
  if(!bubbleScope || caller!=0x1cd589) return result;
  spriteXDelta=bubbleSavedX; spriteYDelta=bubbleSavedY; bubbleProjected=false;
  if(interpolateMotion && !manualBookActive()) {
    auto it=renderPoses.find({nullptr,matrix,1});
    if(it!=renderPoses.end()) {
      const auto &pose=it->second;
      if(pose.frame==frames && pose.result.size()==12 &&
          sameTranspose(pose.current.data(),matrix,3)) {
        out[0]=pose.result[3]; out[1]=pose.result[7]; out[2]=pose.result[11];
      }
    }
  }
  return result;
}
static unsigned __fastcall bubblePanelHook(void *obj,void*,int x,int y,
    int width,int height,float z,float alpha) {
  uintptr_t caller=uintptr_t(__builtin_return_address(0))-gameBase;
  if(bubbleScope && bubbleProjected && caller==0x1cd858) {
    // Native layout clamps the panel at 0/640 and y=32. Keep those bounds
    // exact; move the panel and all following text as one fractional group.
    float dx=(x>0 && x+width<640)?bubbleX-std::trunc(bubbleX):0;
    float dy=y>32?bubbleY-std::trunc(bubbleY):0;
    spriteXDelta=bubbleSavedX+dx*uiWidth()/640.f;
    spriteYDelta=bubbleSavedY+dy*outputHeight/480.f;
    static unsigned traces;
    if(diagnostics && traces++<12)
      log("Popup fractional position=%g,%g delta=%g,%g",bubbleX,bubbleY,dx,dy);
  }
  return bubblePanel(obj,x,y,width,height,z,alpha);
}
static unsigned __cdecl bubbleHook(void *m, int a, int b) {
  bool savedScope=bubbleScope, savedProjected=bubbleProjected;
  float savedX=spriteXDelta,savedY=spriteYDelta;
  float oldBaseX=bubbleSavedX,oldBaseY=bubbleSavedY;
  bubbleScope=a==0 && interpolateMotion; bubbleProjected=false;
  bubbleSavedX=savedX; bubbleSavedY=savedY;
  ++worldDepth;
  auto r = bubble(m, a, b);
  --worldDepth;
  spriteXDelta=savedX; spriteYDelta=savedY;
  bubbleScope=savedScope; bubbleProjected=savedProjected;
  bubbleSavedX=oldBaseX; bubbleSavedY=oldBaseY;
  return r;
}
using VectorFn = float *(__cdecl *)(float *, const float *, const float *);
static VectorFn projectVector;
static float *__cdecl vectorHook(float *out, const float *in,
                                 const float *matrix) {
  if (worldDepth && !manualBookActive() && cameraFrame == frames &&
      !memcmp(matrix, cameraNative, 64)) matrix = cameraRendered;
  auto r = projectVector(out, in, matrix);
  if (worldDepth) out[0] *= float(outputWidth) / uiWidth();
  uintptr_t caller=uintptr_t(__builtin_return_address(0))-gameBase;
  if(bubbleScope && caller==0x1cd5b4 && std::isfinite(out[0]) && std::isfinite(out[1])) {
    bubbleX=(out[0]+1.f)*320.f;
    bubbleY=(1.f-out[1])*240.f;
    bubbleProjected=true;
  }
  return r;
}
using CursorFn = unsigned(__thiscall *)(void *);
using CursorDrawFn = void(__cdecl *)(int, int);
static CursorFn cursor;
static CursorDrawFn cursorDraw;
static void __cdecl cursorDrawHook(int x, int y) {
  float gap = (float(outputWidth) - uiWidth()) / 2;
  cursorDraw(int((float(x) * outputWidth / 640.f - gap) * 640.f / uiWidth()),
             y);
}
static unsigned __fastcall cursorHook(void *obj, void *) {
  ++cursorDepth;
  auto &cb = *reinterpret_cast<CursorDrawFn *>(static_cast<char *>(obj) + 0x3c);
  auto saved = cb;
  cursorDraw = cb;
  if (cb)
    cb = cursorDrawHook;
  auto r = cursor(obj);
  cb = saved;
  --cursorDepth;
  return r;
}
using MouseFn = unsigned(__thiscall *)(void *, int *, void *, void *, void *,
                                       void *, void *, void *);
static MouseFn mouse;
static unsigned __fastcall mouseHook(void *obj, void *, int *pt, void *a,
                                     void *b, void *c, void *e, void *f,
                                     void *g) {
  auto r = mouse(obj, pt, a, b, c, e, f, g);
  if (pt) {
    float px = float(pt[0]) * outputWidth / 640.f,
          py = float(pt[1]) * outputHeight / 480.f;
    float gap = float(outputWidth) - uiWidth(), offset = gap / 2;
    bool activeHud = hudSeen && frames - lastHudFrame <= 2;
    if (activeHud) {
      // Preserve world/camera mouse coordinates; only the anchored interactive
      // right-hand controls use the UI inverse transform in ordinary gameplay.
      if (px >= outputWidth - uiWidth() / 4.f)
        offset = gap;
      else
        return r;
    }
    pt[0] = int((px - offset) * 640.f / uiWidth());
    pt[1] = int(py * 480.f / outputHeight);
  }
  return r;
}
static bool nativeHookFailure;
// Native 77FBE0 calls 7A58B0 with its ECX texture receiver before reading
// six stack arguments. Argument four is coordinates or vertex data by mode.
using ProjectedQuadFn = unsigned(__thiscall *)(void *, uintptr_t, uintptr_t,
                                              uintptr_t, uintptr_t, float, unsigned);
static ProjectedQuadFn projectedQuad;
using ParticleDrawFn = unsigned(__thiscall *)(void *, const float *,
                                              const float *, unsigned);
static ParticleDrawFn particleDraw;
static unsigned __fastcall particleDrawHook(void *obj, void *,
                                            const float *view,
                                            const float *axes,
                                            unsigned category) {
  int sceneWidth = int(outputWidth) + game<int>(0x1de8d58);
  int sceneHeight = int(outputHeight) + game<int>(0x1de8d5c);
  bool scoped = !hudDepth && ((actualViewport.Width == outputWidth &&
                               actualViewport.Height == outputHeight) ||
                              (int(actualViewport.Width) == sceneWidth &&
                               int(actualViewport.Height) == sceneHeight));
  int savedWidth = game<int>(0x508b14), savedHeight = game<int>(0x508b18);
  if (scoped) {
    ++worldParticleDepth;
    game<int>(0x508b14) = actualViewport.Width;
    game<int>(0x508b18) = actualViewport.Height;
    ++projectedGeometryDepth;
    if (interpolateMotion && !manualBookActive() && cameraFrame == frames && view &&
        !memcmp(view, cameraNative, 64))
      view = cameraRendered;
  }
  auto result = particleDraw(obj, view, axes, category);
  if (scoped) {
    --worldParticleDepth;
    --projectedGeometryDepth;
    game<int>(0x508b14) = savedWidth;
    game<int>(0x508b18) = savedHeight;
  }
  static unsigned count;
  if (diagnostics && scoped && count++ < 10)
    log("World particle batch viewport=%ux%u cameraFiltered=%d category=%u",
        actualViewport.Width, actualViewport.Height, view == cameraRendered,
        category);
  return result;
}
static unsigned __fastcall projectedQuadHook(void *texture,void*,
    uintptr_t a,uintptr_t b,uintptr_t c,uintptr_t e,float opacity,unsigned mode) {
  int savedWidth = game<int>(0x508b14), savedHeight = game<int>(0x508b18);
  if (actualViewport.Width && actualViewport.Height) {
    game<int>(0x508b14) = actualViewport.Width;
    game<int>(0x508b18) = actualViewport.Height;
  }
  ++projectedGeometryDepth;
  auto result = projectedQuad(texture, a, b, c, e, opacity, mode);
  --projectedGeometryDepth;
  game<int>(0x508b14) = savedWidth;
  game<int>(0x508b18) = savedHeight;
  static unsigned count;
  if (diagnostics && ++count <= 10)
    log("Projected world quad viewport=%ux%u nativeCanvas=%dx%d",
        actualViewport.Width, actualViewport.Height, savedWidth, savedHeight);
  return result;
}
template <class T>
static bool nativeHook(uintptr_t rva, T detour, T &original) {
  auto addr = reinterpret_cast<void *>(gameBase + rva);
  auto status = MH_CreateHook(addr, reinterpret_cast<void *>(detour),
                              reinterpret_cast<void **>(&original));
  log("Native hook RVA=%lx status=%d", (unsigned long)rva, status);
  if (status != MH_OK)
    nativeHookFailure = true;
  return status == MH_OK;
}
static bool nativeHooksInstalled;
static void installNativeHooks() {
  if (!supported || nativeHooksInstalled)
    return;
  auto initialization = MH_Initialize();
  if (initialization != MH_OK &&
      initialization != MH_ERROR_ALREADY_INITIALIZED) {
    supported = false;
    return;
  }
  nativeHookFailure = false;
  nativeHook(0x392750, solidRectHook, solidRect);
  nativeHook(0x1defb0, fadeHook, nativeFade);
  nativeHookFailure |= MH_CreateHook(reinterpret_cast<void*>(gameBase+0x37fbe0),
      reinterpret_cast<void*>(projectedQuadHook),reinterpret_cast<void**>(&projectedQuad))!=MH_OK;
  nativeHook(0x1cf280, upperHook, upperHud);
  nativeHook(0x1d9b00, lowerHook, lowerHud);
  nativeHook(0x1cdcd0, markerHook, marker);
  nativeHook(0x1cd4f0, bubbleHook, bubble);
  nativeHook(0x37a4a0, vectorHook, projectVector);
  nativeHookFailure |= MH_CreateHook(reinterpret_cast<void*>(gameBase+0x373ef0),
      reinterpret_cast<void*>(bubbleAnchorHook),reinterpret_cast<void**>(&bubbleAnchor))!=MH_OK;
  nativeHookFailure |= MH_CreateHook(reinterpret_cast<void*>(gameBase+0x1e27f0),
      reinterpret_cast<void*>(bubblePanelHook),reinterpret_cast<void**>(&bubblePanel))!=MH_OK;
  nativeHook(0x3a4360, skinHook, uploadSkin);
  auto ss = MH_CreateHook(reinterpret_cast<void *>(gameBase + 0x38ae20),
                          reinterpret_cast<void *>(spriteHook),
                          reinterpret_cast<void **>(&sprite));
  log("Sprite hook status=%d", ss);
  nativeHookFailure |= ss != MH_OK;
  auto particles = MH_CreateHook(reinterpret_cast<void *>(gameBase + 0x325f40),
                                 reinterpret_cast<void *>(particleDrawHook),
                                 reinterpret_cast<void **>(&particleDraw));
  log("Particle batch hook=%d", particles);
  nativeHookFailure |= particles != MH_OK;
  // thiscall has an ECX receiver; the detour's spare EDX argument preserves the
  // ABI.
  auto addr = reinterpret_cast<void *>(gameBase + 0x3aa2a0);
  auto s = MH_CreateHook(addr, reinterpret_cast<void *>(cursorHook),
                         reinterpret_cast<void **>(&cursor));
  log("Cursor hook status=%d", s);
  nativeHookFailure |= s != MH_OK;
  s = MH_CreateHook(reinterpret_cast<void *>(gameBase + 0x380150),
                    reinterpret_cast<void *>(shaderDrawHook),
                    reinterpret_cast<void **>(&shaderDraw));
  log("Shader draw hook status=%d", s);
  nativeHookFailure |= s != MH_OK;
  s = MH_CreateHook(reinterpret_cast<void *>(gameBase + 0x3aa330),
                    reinterpret_cast<void *>(mouseHook),
                    reinterpret_cast<void **>(&mouse));
  log("Mouse hook status=%d", s);
  nativeHookFailure |= s != MH_OK;
  s = MH_CreateHook(reinterpret_cast<void *>(gameBase + 0x350a50),
                    reinterpret_cast<void *>(modelDrawHook),
                    reinterpret_cast<void **>(&modelDraw));
  log("Model draw hook=%d", s);
  nativeHookFailure |= s != MH_OK;
  s = MH_CreateHook(reinterpret_cast<void *>(gameBase + 0x351530),
                    reinterpret_cast<void *>(modelDrawAltHook),
                    reinterpret_cast<void **>(&modelDrawAlt));
  log("Model draw alternate hook=%d", s);
  nativeHookFailure |= s != MH_OK;
  s = MH_CreateHook(reinterpret_cast<void *>(gameBase + 0x3528b0),
                    reinterpret_cast<void *>(modelDrawThirdHook),
                    reinterpret_cast<void **>(&modelDrawThird));
  log("Model draw third hook=%d", s);
  nativeHookFailure |= s != MH_OK;
  s = MH_CreateHook(reinterpret_cast<void *>(gameBase + 0x351ee0),
                    reinterpret_cast<void *>(shadowReceiverHook),
                    reinterpret_cast<void **>(&shadowReceiver));
  log("Shadow receiver hook=%d", s);
  nativeHookFailure |= s != MH_OK;
  auto enabled =
      nativeHookFailure ? MH_ERROR_NOT_CREATED : MH_EnableHook(MH_ALL_HOOKS);
  log("Enable hooks status=%d", enabled);
  if (enabled != MH_OK) {
    MH_DisableHook(MH_ALL_HOOKS);
    MH_RemoveHook(MH_ALL_HOOKS);
    supported = false;
    log("Native hooks unavailable; leaving native rendering unchanged");
  } else
    nativeHooksInstalled = true;
}
#include "camera.hpp"
static gurumin::FramePacer framePacer;
static bool pacingFocused;
static void paceAfterPresent(HRESULT result) {
  if (!supported || FAILED(result) || modernSettings.frameCap <= 0) {
    framePacer.reset();
    return;
  }
  bool focused = GetForegroundWindow() == gameWindow;
  if (focused != pacingFocused) {
    pacingFocused = focused;
    framePacer.reset();
  }
  double deadline = framePacer.next(nowSeconds(), modernSettings.frameCap);
  for (;;) {
    double remaining = deadline - nowSeconds();
    if (remaining <= 0)
      break;
    if (remaining > .002)
      Sleep(DWORD((remaining - .001) * 1000.));
    else if (remaining > .0002)
      SwitchToThread();
    else
      YieldProcessor();
  }
}
static HRESULT WINAPI presentHook(IDirect3DDevice9 *d, const RECT *a,
                                  const RECT *b, HWND w, const RGNDATA *r) {
  ++frames;
  if (supported && !windowSized && gameWindow &&
      ((borderless && windowedDevice) ||
       gurumin::validResolution(modernSettings.resolution))) {
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoA(MonitorFromWindow(gameWindow, MONITOR_DEFAULTTOPRIMARY),
                        &mi)) {
      int width = outputWidth, height = outputHeight;
      if (borderless || !windowedDevice) {
        SetWindowLongA(gameWindow, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowLongA(gameWindow, GWL_EXSTYLE,
                       GetWindowLongA(gameWindow, GWL_EXSTYLE) &
                           ~(WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE));
      } else {
        // Native window creation precedes the late resolution adapter. Its
        // client size must match the new backbuffer, including framed mode.
        RECT rect{0, 0, width, height};
        if (AdjustWindowRectEx(&rect, GetWindowLongA(gameWindow, GWL_STYLE),
                               GetMenu(gameWindow) != nullptr,
                               GetWindowLongA(gameWindow, GWL_EXSTYLE))) {
          width = rect.right - rect.left;
          height = rect.bottom - rect.top;
        }
      }
      SetWindowPos(gameWindow, nullptr,
                   mi.rcMonitor.left +
                       (mi.rcMonitor.right - mi.rcMonitor.left - width) / 2,
                   mi.rcMonitor.top +
                       (mi.rcMonitor.bottom - mi.rcMonitor.top - height) / 2,
                   width, height,
                   SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOACTIVATE);
      windowSized = true;
    }
  }
  LARGE_INTEGER t;
  QueryPerformanceCounter(&t);
  if (diagnostics && frames % 600 == 0) {
    log("Shadow receiver totals entries=%u matched=%u", shadowEntries,
        shadowMatched);
    log("present=%u elapsed=%.3f drawUP=%u transforms=%u constants=%u sim=%u "
        "half=%u",
        frames, double(t.QuadPart - start.QuadPart) / frequency.QuadPart, draws,
        transforms, consts, supported ? game<unsigned>(0x1de47f4) : 0,
        supported ? game<unsigned>(0x1de47f8) : 0);
  }
  HRESULT result = present(d, a, b, w, r);
  paceAfterPresent(result);
  return result;
}
static HRESULT WINAPI transformHook(IDirect3DDevice9 *d,
                                    D3DTRANSFORMSTATETYPE t,
                                    const D3DMATRIX *m) {
  ++transforms;
  static int n;
  if (diagnostics && n < 12 && t == D3DTS_PROJECTION) {
    log("projection %g %g %g %g", m->_11, m->_22, m->_33, m->_34);
    ++n;
  }
  return setTransform(d, t, m);
}
static bool nearestAssetSampler(IDirect3DDevice9 *d) {
  if(!supported || shadowReceiverDepth) return false;
  bool ui=activeSpriteCaller && !projectedGeometryDepth && !worldMaterialDepth &&
    activeSpriteCaller!=0x1df19b && activeSpriteCaller!=0x1f3828;
  bool world=!ui && (worldMaterialDepth || worldParticleDepth || projectedGeometryDepth);
  if((!ui && !world) || (ui?modernSettings.uiFiltering:modernSettings.worldFiltering))
    return false;
  // Native stage 0 is the asset color sampler. Lookup/effect samplers at
  // stages 1+ retain their policy. Require a named native asset resource,
  // and for model draws membership in the native material texture arrays.
  uintptr_t resource=game<uintptr_t>(0x1de47ec);
  uintptr_t offset=resource-(gameBase+0x198f6a0);
  if(offset>=1600*0x38u || offset%0x38u) return false;
  int handle=int(offset/0x38u);
  auto name=reinterpret_cast<const char*>(gameBase+0x128c7d0+handle*0x40);
  if(!name[0] || !strncmp(name,"shadow",6) || !strcmp(name,"crtcc")) return false;
  if(worldMaterialDepth) {
    if(!readableRange(materialDescriptor,0x48)) return false;
    auto descriptor=static_cast<const unsigned char*>(materialDescriptor);
    int count=*reinterpret_cast<const int*>(descriptor+0x44);
    auto primary=*reinterpret_cast<const int*const*>(descriptor+0x14);
    auto secondary=*reinterpret_cast<const int*const*>(descriptor+0x18);
    if(count<=0 || count>4096 ||
        !readableRange(primary,std::size_t(count)*sizeof(int))) return false;
    if(secondary && !readableRange(secondary,std::size_t(count)*sizeof(int)))
      secondary=nullptr;
    bool member=false;
    for(int i=0;i<count;++i)
      if(primary[i]==handle || (secondary && secondary[i]==handle)) { member=true; break; }
    if(!member) return false;
  }
  IDirect3DBaseTexture9 *texture=nullptr;
  if(FAILED(d->GetTexture(0,&texture)) || !texture) return false;
  bool eligible=false;
  if(texture->GetType()==D3DRTYPE_TEXTURE) {
    D3DSURFACE_DESC desc{};
    eligible=SUCCEEDED(static_cast<IDirect3DTexture9*>(texture)->GetLevelDesc(0,&desc)) &&
      !(desc.Usage & D3DUSAGE_RENDERTARGET);
  }
  texture->Release();
  return eligible;
}
static HRESULT submitUP(IDirect3DDevice9 *d,D3DPRIMITIVETYPE t,UINT n,
                        const void *data,UINT stride) {
  PointSampler sampling(d,nearestAssetSampler(d));
  return drawUP(d,t,n,data,stride);
}
using DrawIndexedFn=HRESULT(WINAPI*)(IDirect3DDevice9*,D3DPRIMITIVETYPE,INT,UINT,UINT,UINT,UINT);
using DrawPrimitiveFn=HRESULT(WINAPI*)(IDirect3DDevice9*,D3DPRIMITIVETYPE,UINT,UINT);
using DrawIndexedUPFn=HRESULT(WINAPI*)(IDirect3DDevice9*,D3DPRIMITIVETYPE,UINT,UINT,UINT,
                                     const void*,D3DFORMAT,const void*,UINT);
static DrawIndexedFn drawIndexed;
static DrawPrimitiveFn drawPrimitive;
static DrawIndexedUPFn drawIndexedUP;
static HRESULT WINAPI indexedHook(IDirect3DDevice9 *d,D3DPRIMITIVETYPE t,INT base,
                                  UINT min,UINT vertices,UINT start,UINT count) {
  PointSampler sampling(d,nearestAssetSampler(d));
  return drawIndexed(d,t,base,min,vertices,start,count);
}
static HRESULT WINAPI primitiveHook(IDirect3DDevice9 *d,D3DPRIMITIVETYPE t,UINT start,UINT count) {
  PointSampler sampling(d,nearestAssetSampler(d));
  return drawPrimitive(d,t,start,count);
}
static HRESULT WINAPI indexedUPHook(IDirect3DDevice9 *d,D3DPRIMITIVETYPE t,UINT min,
                                    UINT vertices,UINT count,const void *indices,
                                    D3DFORMAT format,const void *data,UINT stride) {
  PointSampler sampling(d,nearestAssetSampler(d));
  return drawIndexedUP(d,t,min,vertices,count,indices,format,data,stride);
}
static HRESULT WINAPI drawHook(IDirect3DDevice9 *d, D3DPRIMITIVETYPE t, UINT n,
                               const void *data, UINT stride) {
  ++draws;
  DWORD fvf = 0;
  d->GetFVF(&fvf);
  auto caller=uintptr_t(__builtin_return_address(0))-gameBase;
  if(supported && modernSettings.antiAliasing && caller==0x3a7e21 &&
     fvf==0x4c4 && stride==56 && t==D3DPT_TRIANGLESTRIP && n==2 &&
     game<uintptr_t>(0x1de4830)==gameBase+0x1cf280 &&
     !game<int>(0x515ec4) && !game<int>(0x1da34e4)) {
    unsigned parity=game<unsigned>(0x1de8d4c);
    IDirect3DBaseTexture9 *source=nullptr;
    IDirect3DSurface9 *destination=nullptr;
    bool eligible=parity<=1 && SUCCEEDED(d->GetTexture(0,&source)) && source &&
      source==game<IDirect3DTexture9*>(0x1da35d8+parity*4) &&
      SUCCEEDED(d->GetRenderTarget(0,&destination)) && destination &&
      destination==game<IDirect3DSurface9*>(0x1da3544);
    if(source) source->Release();
    if(destination) destination->Release();
    if(eligible) {
      if(diagnostics) {
        static bool tracedColor;
        if(!tracedColor) {
          DWORD decode=~0u,encode=~0u;
          d->GetSamplerState(0,D3DSAMP_SRGBTEXTURE,&decode);
          d->GetRenderState(D3DRS_SRGBWRITEENABLE,&encode);
          log("Scene AA color contract samplerSRGB=%lu targetSRGB=%lu",
              (unsigned long)decode,(unsigned long)encode);
          tracedColor=true;
        }
      }
      HRESULT result=sceneAA.draw(d,t,n,data,stride,modernSettings.antiAliasing,drawUP);
      static unsigned applied;
      if(sceneAA.lastApplied() && (applied++==0 || (diagnostics && frames%600==0)))
        log("FXAA quality=%d applied before upper HUD restore=%08lx",modernSettings.antiAliasing,
            (unsigned long)sceneAA.lastRestoreResult());
      if(diagnostics && !sceneAA.lastApplied()) {
        static unsigned skipped;
        if(skipped++<4) {
          DWORD colorOp=0,alphaOp=0,stage1=0;
          d->GetTextureStageState(0,D3DTSS_COLOROP,&colorOp);
          d->GetTextureStageState(0,D3DTSS_ALPHAOP,&alphaOp);
          d->GetTextureStageState(1,D3DTSS_COLOROP,&stage1);
          auto first=static_cast<const unsigned char*>(data);
          log("FXAA native fallback: colorOp=%lu alphaOp=%lu stage1=%lu diffuse=%08lx UV=%g,%g",
            (unsigned long)colorOp,(unsigned long)alphaOp,(unsigned long)stage1,
            (unsigned long)*reinterpret_cast<const DWORD*>(first+16),
            *reinterpret_cast<const float*>(first+24),*reinterpret_cast<const float*>(first+28));
        }
      }
      return result;
    }
  }
  static int z;
  if (diagnostics && z < 20) {
    auto f = static_cast<const float *>(data);
    log("drawUP type=%u count=%u stride=%u fvf=%lx first=%g,%g,%g,%g", t, n,
        stride, (unsigned long)fvf, f[0], f[1], f[2], f[3]);
    ++z;
  }
  D3DVIEWPORT9 v;
  d->GetViewport(&v);
  if (diagnostics && !hudDepth && hudSeen && frames - lastHudFrame <= 2 &&
      fvf == 0x1c4) {
    static std::unordered_map<uintptr_t, unsigned> sources;
    auto caller = uintptr_t(__builtin_return_address(0)) - gameBase;
    auto sourceKey =
        caller ^ (uintptr_t(v.Width) << 16) ^ (uintptr_t(v.Height) << 4);
    if (sources[sourceKey]++ < 3) {
      auto f = static_cast<const float *>(data);
      log("World DrawUP caller=%lx stride=%u n=%u viewport=%ux%u canvas=%dx%d "
          "shaderScope=%d quadScope=%d model=%.32s xy=%.3f,%.3f",
          (unsigned long)caller, stride, n, v.Width, v.Height,
          game<int>(0x508b14), game<int>(0x508b18), worldShaderDepth,
          projectedGeometryDepth, modelOwner ? modelOwner : "none", f[0], f[1]);
    }
  }
  if (supported && !projectedGeometryDepth && !worldShaderDepth &&
      fvf == 0x1c4 && v.Width == outputWidth && v.Height == outputHeight &&
      stride >= 16) {
    UINT count = t == D3DPT_TRIANGLESTRIP || t == D3DPT_TRIANGLEFAN ? n + 2
                 : t == D3DPT_TRIANGLELIST                          ? n * 3
                 : t == D3DPT_LINELIST                              ? n * 2
                 : t == D3DPT_LINESTRIP                             ? n + 1
                                                                    : n;
    if (count < 20000) {
      std::vector<unsigned char> copy(static_cast<const unsigned char *>(data),
                                      static_cast<const unsigned char *>(data) +
                                          count * stride);
      float gap = float(v.Width) - uiWidth(), offset = gap / 2.f;
      float xmin = 1e9f, xmax = -1e9f, ymin = 1e9f, ymax = -1e9f;
      for (UINT i = 0; i < count; ++i) {
        auto f = reinterpret_cast<const float *>(copy.data() + i * stride);
        xmin = std::min(xmin, f[0]);
        xmax = std::max(xmax, f[0]);
        ymin = std::min(ymin, f[1]);
        ymax = std::max(ymax, f[1]);
      }
      float x = (xmin + xmax) * 320.f / uiWidth(),
            y = (ymin + ymax) * 240.f / outputHeight;
      if (hudDepth && !worldDepth && !cursorDepth) {
        if (x >= 480.f || (y < 23.f && x >= 390.f))
          offset = gap;
        else if ((y < 55.f && x < 165.f) ||
                 (y > 375.f && x < (game<int>(0x515ec4) ? 480.f : 340.f)))
          offset = 0;
        static unsigned loggedFrame = 0;
        static int k = 0;
        int menu = game<int>(0x515ec4);
        if (menu && loggedFrame == 0)
          loggedFrame = frames + 1;
        if (diagnostics && menu && frames + 1 == loggedFrame && k++ < 100)
          log("HUD bounds %.1f %.1f %.1f %.1f offset=%.1f color=%08lx", xmin,
              ymin, xmax, ymax, offset,
              (unsigned long)*reinterpret_cast<const DWORD *>(copy.data() +
                                                              16));
      }
      bool titlePattern = titlePatternDepth || activeSpriteCaller == 0x22d463 ||
                          activeSpriteCaller == 0x22d53d;
      bool iris = activeSpriteCaller == 0x1df19b;
      if (count == 4 && stride == 32 &&
          (titlePattern || iris || fullscreenSolidDepth)) {
        float umin = 1e9f, umax = -1e9f;
        for (UINT i = 0; i < count; ++i) {
          auto f = reinterpret_cast<float *>(copy.data() + i * stride);
          umin = std::min(umin, f[6]); umax = std::max(umax, f[6]);
        }
        // Keep the native pattern/iris pixel density and scrolling phase.
        // The native iris clamps edge texels and multiplies destination RGB.
        // Preserve that sampling and its circular mask; centered-canvas
        // translation is independent of HUD anchoring.
        offset=gap/2.f;
        float left = iris ? std::min(xmin + offset, 0.f) : 0.f;
        float right = iris ? std::max(xmax + offset, float(outputWidth))
                           : float(outputWidth);
        for (UINT i = 0; i < count; ++i) {
          auto f = reinterpret_cast<float *>(copy.data() + i * stride);
          float oldX = f[0] + offset;
          f[0] = left + (f[0] - xmin) / (xmax - xmin) * (right - left);
          if (!fullscreenSolidDepth)
            f[6] += (f[0] - oldX) * (umax - umin) / (xmax - xmin);
        }
        if (diagnostics) {
          static unsigned traces;
          if (traces++ < 12) {
            DWORD address = 0, src = 0, dest = 0;
            d->GetSamplerState(0, D3DSAMP_ADDRESSU, &address);
            d->GetRenderState(D3DRS_SRCBLEND, &src);
            d->GetRenderState(D3DRS_DESTBLEND, &dest);
            log("Canvas cover caller=%lx fullSolid=%d pattern=%d iris=%d "
                "bounds=%g,%g -> %g,%g uv=%g,%g address=%lu blend=%lu,%lu",
                (unsigned long)activeSpriteCaller, fullscreenSolidDepth,
                titlePattern, iris, xmin, xmax, left, right, umin, umax,
                (unsigned long)address, (unsigned long)src, (unsigned long)dest);
          }
        }
      } else if (hudDepth && count == 4 &&
                 (activeSpriteCaller == 0x1cf352 || activeSpriteCaller == 0x1cf3bf)) {
        float sceneWidth = float(outputWidth) + game<int>(0x1de8d58);
        float sceneHeight = float(outputHeight) + game<int>(0x1de8d5c);
        bool right = activeSpriteCaller == 0x1cf352;
        for (UINT i = 0; i < count; ++i) {
          auto f = reinterpret_cast<float *>(copy.data() + i * stride);
          f[0] = (f[0] - xmin) / (xmax - xmin) *
                     (right ? outputWidth - sceneWidth : sceneWidth) +
                 (right ? sceneWidth : 0.f);
          f[1] = (f[1] - ymin) / (ymax - ymin) *
                     (right ? outputHeight : outputHeight - sceneHeight) +
                 (right ? 0.f : sceneHeight);
        }
      } else
        for (UINT i = 0; i < count; ++i) {
          auto f = reinterpret_cast<float *>(copy.data() + i * stride);
          f[0] += offset + spriteXDelta;
          f[1] += spriteYDelta;
        }
      static unsigned tracedCenter = 0, tracedNotes = 0;
      if (diagnostics && ((activeSpriteCaller == 0x1d0de9 &&
                           (tracedCenter++ < 180 || frames % 1800 < 180)) ||
                          (activeSpriteCaller == 0x1d0362 &&
                           (tracedNotes++ < 300 || frames % 1800 < 60)))) {
        auto f = reinterpret_cast<const float *>(copy.data());
        log("Rhythm draw frame=%u t=%.6f caller=%lx native=%d phaseDelta=%.5f "
            "input=%.3f,%.3f final=%.3f,%.3f offset=%.3f,%.3f",
            frames, nowSeconds(), (unsigned long)activeSpriteCaller,
            game<int>(0x5e160c), rhythmDelta, xmin, ymin, f[0], f[1],
            spriteXDelta, spriteYDelta);
      }
      return submitUP(d, t, n, copy.data(), stride);
    }
  }
  return submitUP(d, t, n, data, stride);
}
static HRESULT WINAPI constHook(IDirect3DDevice9 *d, UINT reg,
                                const float *data, UINT n) {
  ++consts;
  static int z;
  if (diagnostics && z < 12) {
    log("VS constants reg=%u n=%u first=%g,%g,%g,%g", reg, n, data[0], data[1],
        data[2], data[3]);
    if (reg == 0 && n == 4) {
      for (int i = 0; i < 16; i += 4)
        log("matrix %g,%g,%g,%g", data[i], data[i + 1], data[i + 2],
            data[i + 3]);
    }
    ++z;
  }
  if (uploadingSkin && reg == 11 && n == unsigned(currentSkin.count * 3) &&
      n <= 78) {
    auto &p = poses[currentSkin];
    if (p.frame != frames) {
      unsigned h = 2166136261u;
      for (unsigned i = 0; i < n * 4; ++i) {
        uint32_t bits;
        memcpy(&bits, data + i, 4);
        h = (h ^ bits) * 16777619u;
      }
      if (h != p.hash)
        ++p.unique;
      ++p.seen;
      p.hash = h;
      p.frame = frames;
      p.tick = game<unsigned>(0x1de47f4);
      if (diagnostics && p.seen % 600 == 0)
        log("Pose key=%p/%p bones=%d samples=%u changed=%u tick=%u",
            currentSkin.bones, currentSkin.model, currentSkin.count, p.seen,
            p.unique, p.tick);
    }
  }
  if (uploadingSkin && reg == 11 && n == unsigned(currentSkin.count * 3) &&
      n <= 78 && n > 0) {
    const float *submitted = data;
    if (interpolateMotion) {
      auto smoothed = smoothPacked(currentSkin, data, n);
      if (smoothed)
        submitted = smoothed;
    }
    HRESULT result = setConst(d, reg, submitted, n);
    if (SUCCEEDED(result))
      rememberSubmittedPalette(data, submitted, n);
    return result;
  }
  // World and camera matrices are now corrected once at the native draw entry,
  // before mode-specific register layouts and dependent matrices are assembled.
  return setConst(d, reg, data, n);
}
using TextureFn = HRESULT(WINAPI *)(IDirect3DDevice9 *, UINT, UINT, UINT, DWORD,
                                    D3DFORMAT, D3DPOOL, IDirect3DTexture9 **,
                                    HANDLE *);
using TargetFn = HRESULT(WINAPI *)(IDirect3DDevice9 *, UINT,
                                   IDirect3DSurface9 *);
using ViewportFn = HRESULT(WINAPI *)(IDirect3DDevice9 *, const D3DVIEWPORT9 *);
static TextureFn createTexture;
static TargetFn setTarget;
static ViewportFn setViewport;
using DepthFn = HRESULT(WINAPI *)(IDirect3DDevice9 *, UINT, UINT, D3DFORMAT,
                                  D3DMULTISAMPLE_TYPE, DWORD, BOOL,
                                  IDirect3DSurface9 **, HANDLE *);
static DepthFn createDepth;
using ClearFn = HRESULT(WINAPI *)(IDirect3DDevice9 *, DWORD, const D3DRECT *,
                                  DWORD, D3DCOLOR, float, DWORD);
static ClearFn clear;
static HRESULT WINAPI clearHook(IDirect3DDevice9 *d, DWORD n,
                                const D3DRECT *rect, DWORD flags,
                                D3DCOLOR color, float z, DWORD stencil) {
  static int k;
  if (diagnostics && k++ < 35)
    log("Clear n=%lu flags=%lu color=%08lx rect=%ld,%ld,%ld,%ld",
        (unsigned long)n, (unsigned long)flags, (unsigned long)color,
        rect ? rect[0].x1 : 0, rect ? rect[0].y1 : 0, rect ? rect[0].x2 : 0,
        rect ? rect[0].y2 : 0);
  return clear(d, n, rect, flags, color, z, stencil);
}
static HRESULT WINAPI depthHook(IDirect3DDevice9 *d, UINT w, UINT h,
                                D3DFORMAT f, D3DMULTISAMPLE_TYPE m, DWORD q,
                                BOOL discard, IDirect3DSurface9 **o,
                                HANDLE *shared) {
  auto extent=shadowDepthSize(w,h,uintptr_t(__builtin_return_address(0))-gameBase);
  w=extent.width; h=extent.height;
  HRESULT hr = createDepth(d, w, h, f, m, q, discard, o, shared);
  log("Depth %ux%u format=%u msaa=%u hr=%08lx", w, h, f, m, (unsigned long)hr);
  return hr;
}

static HRESULT WINAPI textureHook(IDirect3DDevice9 *d, UINT w, UINT h,
                                  UINT levels, DWORD usage, D3DFORMAT fmt,
                                  D3DPOOL pool, IDirect3DTexture9 **o,
                                  HANDLE *shared) {
  if (usage & D3DUSAGE_RENDERTARGET)
    log("RT texture %ux%u usage=%lx", w, h, (unsigned long)usage);
  HRESULT hr = createTexture(d, w, h, levels, usage, fmt, pool, o, shared);
  if ((usage & D3DUSAGE_RENDERTARGET) || FAILED(hr))
    log("Texture result %ux%u hr=%08lx", w, h, (unsigned long)hr);
  return hr;
}
static HRESULT WINAPI targetHook(IDirect3DDevice9 *d, UINT index,
                                 IDirect3DSurface9 *t) {
  static int z;
  if (diagnostics && z < 40 && t) {
    D3DSURFACE_DESC desc;
    t->GetDesc(&desc);
    log("SetRT index=%u %ux%u", index, desc.Width, desc.Height);
    ++z;
  }
  return setTarget(d, index, t);
}
static HRESULT WINAPI viewportHook(IDirect3DDevice9 *d, const D3DVIEWPORT9 *v) {
  static int z;
  if (diagnostics && z < 40) {
    log("Viewport %u,%u %ux%u", v->X, v->Y, v->Width, v->Height);
    ++z;
  }
  HRESULT result = setViewport(d, v);
  if (SUCCEEDED(result))
    actualViewport = *v;
  if (supported && SUCCEEDED(result) && v->Width == outputWidth &&
      v->Height == outputHeight) {
    game<int>(0x508b14) =
        int(std::min(float(v->Width), float(v->Height) * 4.f / 3.f));
    game<int>(0x508b18) = v->Height;
    game<float>(0x508b20) = 1.f;
  }
  return result;
}
using ResetFn = HRESULT(WINAPI *)(IDirect3DDevice9 *, D3DPRESENT_PARAMETERS *);
static ResetFn resetDevice;
static HRESULT WINAPI resetHook(IDirect3DDevice9 *d, D3DPRESENT_PARAMETERS *p) {
  // A numeric cap must not silently turn native VSync off.
  if (supported && modernSettings.frameCap < 0)
    p->PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
  framePacer.reset();
  sceneAA.reset();
  resetCameraControls();
  HRESULT result = resetDevice(d, p);
  if (SUCCEEDED(result)) {
    shadowReset(true);
    outputWidth = p->BackBufferWidth;
    outputHeight = p->BackBufferHeight;
    windowedDevice = p->Windowed;
    windowSized = false;
    renderPoses.clear();
    poses.clear();
    skinBodies.clear();
    sharedFrame = sharedTick = cameraFrame = ~0u;
    hudSeen = false;
  }
  log("Reset %ux%u hr=%08lx", p->BackBufferWidth, p->BackBufferHeight,
      (unsigned long)result);
  return result;
}
static HRESULT WINAPI deviceHook(IDirect3D9 *d, UINT a, D3DDEVTYPE type, HWND w,
                                 DWORD flags, D3DPRESENT_PARAMETERS *p,
                                 IDirect3DDevice9 **out) {
  gameWindow = w;
  windowedDevice = p->Windowed;
  outputWidth = p->BackBufferWidth;
  outputHeight = p->BackBufferHeight;
  // A numeric cap must not silently turn native VSync off.
  if (supported && modernSettings.frameCap < 0)
    p->PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
  log("CreateDevice %ux%u windowed=%d refresh=%u interval=%u",
      p->BackBufferWidth, p->BackBufferHeight, p->Windowed,
      p->FullScreen_RefreshRateInHz, p->PresentationInterval);
  HRESULT h = createDevice(d, a, type, w, flags, p, out);
  if (FAILED(h) && supported &&
      gurumin::validResolution(modernSettings.resolution)) {
    D3DDISPLAYMODE desktop{};
    if (SUCCEEDED(d->GetAdapterDisplayMode(a, &desktop))) {
      log("Selected device mode failed (%08lx); retrying desktop windowed",
          (unsigned long)h);
      if (!setResolutionTextureLimit(
              std::max({2048u, desktop.Width, desktop.Height})))
        return h;
      p->Windowed = TRUE;
      p->BackBufferWidth = desktop.Width;
      p->BackBufferHeight = desktop.Height;
      p->BackBufferFormat = D3DFMT_UNKNOWN;
      p->FullScreen_RefreshRateInHz = 0;
      game<int>(0x1de4c40) = 0;
      effectiveResolution({int(desktop.Width), int(desktop.Height)});
      h = createDevice(d, a, type, w, flags, p, out);
      outputWidth = p->BackBufferWidth;
      outputHeight = p->BackBufferHeight;
      windowedDevice = p->Windowed;
    }
  }
  log("CreateDevice HRESULT=%08lx", (unsigned long)h);
  if (SUCCEEDED(h)) {
    hook(*out, 16, resetHook, resetDevice);
    hook(*out, 17, presentHook, present);
    hook(*out, 44, transformHook, setTransform);
    hook(*out, 83, drawHook, drawUP);
    hook(*out, 81, primitiveHook, drawPrimitive);
    hook(*out, 82, indexedHook, drawIndexed);
    hook(*out, 84, indexedUPHook, drawIndexedUP);
    hook(*out, 94, constHook, setConst);
    hook(*out, 23, textureHook, createTexture);
    hook(*out, 29, depthHook, createDepth);
    hook(*out, 37, targetHook, setTarget);
    hook(*out, 47, viewportHook, setViewport);
    hook(*out, 43, clearHook, clear);
    installNativeHooks();
    if(supported && nativeHooksInstalled && !installShadowHooks())
      log("Optional high-resolution shadow hooks unavailable; native retained");
    installCameraHooks();
  }
  return h;
}
// Check the complete supported executable, allowing only our five display
// immediates and mode label. Timestamp alone does not establish ABI
// compatibility.
static bool verifyExecutable() {
  char path[MAX_PATH];
  if (!GetModuleFileNameA(nullptr, path, MAX_PATH))
    return false;
  FILE *f = fopen(path, "rb");
  if (!f)
    return false;
  std::vector<unsigned char> bytes(6029312);
  bool read = fread(bytes.data(), 1, bytes.size(), f) == bytes.size() &&
              fgetc(f) == EOF;
  fclose(f);
  if (!read)
    return false;
  const unsigned offsets[] = {0x209982, 0x20998c, 0x209996, 0x31b81e, 0x31b832};
  const unsigned stock[] = {1920, 1080, 1920, 2048, 2048};
  for (unsigned i = 0; i < 5; ++i)
    memcpy(bytes.data() + offsets[i], stock + i, 4);
  memcpy(bytes.data() + 0x567559, "1920  x  1080", 13);
  HCRYPTPROV provider = 0;
  HCRYPTHASH hash = 0;
  unsigned char digest[32];
  DWORD count = sizeof(digest);
  bool valid = CryptAcquireContextA(&provider, nullptr, nullptr, PROV_RSA_AES,
                                    CRYPT_VERIFYCONTEXT) &&
               CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash) &&
               CryptHashData(hash, bytes.data(), DWORD(bytes.size()), 0) &&
               CryptGetHashParam(hash, HP_HASHVAL, digest, &count, 0);
  if (hash)
    CryptDestroyHash(hash);
  if (provider)
    CryptReleaseContext(provider, 0);
  const unsigned char expected[] = {
      0x4a, 0x27, 0xc7, 0x27, 0x16, 0x0d, 0x25, 0x65, 0xf0, 0x24, 0x88,
      0x88, 0x3e, 0x86, 0xa3, 0x4a, 0xc6, 0xe0, 0xf6, 0x50, 0xae, 0xe5,
      0x28, 0xf0, 0xa1, 0xe2, 0x40, 0x17, 0xee, 0xdc, 0x5e, 0x9e};
  return valid && count == sizeof(expected) &&
         !memcmp(digest, expected, sizeof(expected));
}
#include "launcher.hpp"
static unsigned codeImmediate(uintptr_t rva) {
  unsigned value;
  memcpy(&value, reinterpret_cast<const void *>(gameBase + rva), sizeof(value));
  return value;
}
static bool patchTextureLimit(uintptr_t rva, unsigned value) {
  auto target = reinterpret_cast<void *>(gameBase + rva);
  DWORD old;
  if (!VirtualProtect(target, sizeof(value), PAGE_EXECUTE_READWRITE, &old))
    return false;
  memcpy(target, &value, sizeof(value));
  DWORD unused;
  VirtualProtect(target, sizeof(value), old, &unused);
  FlushInstructionCache(GetCurrentProcess(), target, sizeof(value));
  return true;
}
static bool setResolutionTextureLimit(unsigned limit) {
  unsigned oldWidth = codeImmediate(0x31c41e),
           oldHeight = codeImmediate(0x31c432);
  if (patchTextureLimit(0x31c41e, limit) && patchTextureLimit(0x31c432, limit))
    return true;
  patchTextureLimit(0x31c41e, oldWidth);
  patchTextureLimit(0x31c432, oldHeight);
  return false;
}
static void effectiveResolution(gurumin::Resolution r) {
  game<int>(0x508b0c) = r.width;
  game<int>(0x508b10) = r.height;
  game<int>(0x1de480c) = std::max(r.width, r.height);
  // Same dimension dependency as native startup, before it builds projection
  // and resources. The viewport hook later establishes the proportional UI.
  game<float>(0x508b20) = game<float>(0x44dbb8) / (float(r.height) / r.width);
}
static void applyModernResolution(IDirect3D9 *d) {
  if (!supported || !gurumin::validResolution(modernSettings.resolution))
    return;
  auto requested = modernSettings.resolution, effective = requested;
  D3DCAPS9 caps{};
  D3DDISPLAYMODE desktop{};
  if (FAILED(d->GetDeviceCaps(0, D3DDEVTYPE_HAL, &caps)) ||
      FAILED(d->GetAdapterDisplayMode(0, &desktop))) {
    log("Cannot validate modern resolution; preserving native mode");
    return;
  }
  int edge = std::max(effective.width, effective.height);
  int maxEdge = int(std::min(caps.MaxTextureWidth, caps.MaxTextureHeight));
  if (maxEdge < 640) {
    log("Texture capabilities too small; preserving native mode");
    return;
  }
  bool fullscreen = game<int>(0x1de4c40) != 0;
  bool modeFound = !fullscreen;
  if (fullscreen) {
    for (D3DFORMAT format : {desktop.Format, D3DFMT_X8R8G8B8}) {
      UINT count = d->GetAdapterModeCount(0, format);
      for (UINT i = 0; i < count; ++i) {
        D3DDISPLAYMODE m{};
        if (SUCCEEDED(d->EnumAdapterModes(0, format, i, &m)) &&
            m.Width == UINT(effective.width) &&
            m.Height == UINT(effective.height))
          modeFound = true;
      }
    }
  }
  if (edge > maxEdge || !modeFound) {
    effective = {int(desktop.Width), int(desktop.Height)};
    if (std::max(effective.width, effective.height) > maxEdge)
      effective = {std::min(1280, maxEdge), std::min(720, maxEdge)};
    game<int>(0x1de4c40) = 0;
    log("Requested mode unavailable; falling back to desktop windowed");
  } else if (!fullscreen && borderless &&
             (effective.width > int(desktop.Width) ||
              effective.height > int(desktop.Height))) {
    // Borderless output must fit the visible desktop. Preserve the requested
    // value for a later launch on the user's ultrawide monitor.
    effective.width = std::min(effective.width, int(desktop.Width));
    effective.height = std::min(effective.height, int(desktop.Height));
    log("Borderless mode exceeds desktop; using visible desktop bounds");
  }
  unsigned limit =
      unsigned(std::max({2048, effective.width, effective.height}));
  // Both checked allocator operands must change together. If either protection
  // fails, preserve the original limits and native mode.
  if (!setResolutionTextureLimit(limit)) {
    game<int>(0x1de4c40) = fullscreen;
    log("Texture limit update failed; preserving native resolution");
    return;
  }
  effectiveResolution(effective);
  log("Resolution requested=%dx%d effective=%dx%d internalEdge=%d cap=%d",
      requested.width, requested.height, effective.width, effective.height,
      std::max(effective.width, effective.height), modernSettings.frameCap);
}
extern "C" IDirect3D9 *WINAPI Direct3DCreate9(UINT version) {
  static auto real = []() {
    char path[MAX_PATH];
    GetSystemDirectoryA(path, MAX_PATH);
    strcat(path, "\\d3d9.dll");
    auto lib = LoadLibraryA(path);
    IDirect3D9 *(WINAPI * fn)(UINT) = nullptr;
    FARPROC address = GetProcAddress(lib, "Direct3DCreate9");
    static_assert(sizeof(fn) == sizeof(address));
    memcpy(&fn, &address, sizeof(fn));
    return fn;
  }();
  if (!real) {
    log("Unable to load system D3D9");
    return nullptr;
  }
  char ini[MAX_PATH];
  GetModuleFileNameA(GetModuleHandleA(nullptr), ini, MAX_PATH);
  auto slash = strrchr(ini, '\\');
  if (slash)
    strcpy(slash + 1, "GuruminModern.ini");
  interpolateMotion =
      GetPrivateProfileIntA("GuruminModern", "InterpolateMotion", 1, ini) != 0;
  smoothRhythm =
      GetPrivateProfileIntA("GuruminModern", "SmoothRhythm", 1, ini) != 0;
  borderless =
      GetPrivateProfileIntA("GuruminModern", "Borderless", 1, ini) != 0;
  diagnostics =
      GetPrivateProfileIntA("GuruminModern", "Diagnostics", 0, ini) != 0;
  loadModernSettings();
  gameBase = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
  auto nt = reinterpret_cast<IMAGE_NT_HEADERS *>(
      gameBase + reinterpret_cast<IMAGE_DOS_HEADER *>(gameBase)->e_lfanew);
  supported = nt->FileHeader.TimeDateStamp == 0x553e3cc7 && verifyExecutable();
  log("PE timestamp=%lx supported=%d",
      (unsigned long)nt->FileHeader.TimeDateStamp, supported);
  QueryPerformanceFrequency(&frequency);
  QueryPerformanceCounter(&start);
  log("Gurumin Modern motion=%d rhythm=%d borderless=%d diagnostics=%d",
      interpolateMotion, smoothRhythm, borderless, diagnostics);
  installFaultTrace();
  auto d = real(version);
  if (d) {
    applyModernResolution(d);
    hook(d, 16, deviceHook, createDevice);
  }
  return d;
}
BOOL WINAPI DllMain(HINSTANCE h, DWORD why, void *) {
  if (why == DLL_PROCESS_ATTACH) {
    DisableThreadLibraryCalls(h);
    installLauncherBootstrap();
  }
  return TRUE;
}
