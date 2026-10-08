// Real D3D9 fixture: no game code or proxy is loaded.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include "../src/antialias.hpp"
#include "../src/filtering.hpp"
#include <cassert>
#include <cstring>
#include <vector>

namespace {
struct Vertex {
  float x, y, z, rhw;
  DWORD diffuse, specular;
  float u0, v0, u1, v1, u2, v2, u3, v3;
};
static_assert(sizeof(Vertex) == 56, "verified FVF 0x4C4 layout");

unsigned calls = 0;
HRESULT WINAPI nativeDraw(IDirect3DDevice9 *device, D3DPRIMITIVETYPE type,
                          UINT count, const void *vertices, UINT stride) {
  ++calls;
  return device->DrawPrimitiveUP(type, count, vertices, stride);
}

void require(HRESULT value) { assert(SUCCEEDED(value)); }

unsigned channel(DWORD pixel, unsigned shift) { return (pixel >> shift) & 0xffu; }

void fillDiagonal(IDirect3DTexture9 *source, unsigned width, unsigned height) {
  D3DLOCKED_RECT lock{};
  require(source->LockRect(0, &lock, nullptr, 0));
  for (unsigned y = 0; y != height; ++y) {
    DWORD *row = reinterpret_cast<DWORD *>(
        static_cast<unsigned char *>(lock.pBits) + y * lock.Pitch);
    for (unsigned x = 0; x != width; ++x) {
      const bool red = x + y < 40;
      const unsigned alpha = 48 + ((x * 5 + y * 3) & 0x9f);
      row[x] = D3DCOLOR_ARGB(alpha, red ? 208 : 0, 0, red ? 0 : 208);
    }
  }
  require(source->UnlockRect(0));
}

void fillFlat(IDirect3DTexture9 *source, unsigned width, unsigned height) {
  D3DLOCKED_RECT lock{};
  require(source->LockRect(0, &lock, nullptr, 0));
  for (unsigned y = 0; y != height; ++y) {
    DWORD *row = reinterpret_cast<DWORD *>(
        static_cast<unsigned char *>(lock.pBits) + y * lock.Pitch);
    for (unsigned x = 0; x != width; ++x)
      row[x] = D3DCOLOR_ARGB(0x7d, 80, 100, 120);
  }
  require(source->UnlockRect(0));
}

void fillPadding(IDirect3DTexture9 *source, unsigned width, unsigned height) {
  D3DLOCKED_RECT lock{};
  require(source->LockRect(0, &lock, nullptr, 0));
  for (unsigned y = 0; y != height; ++y) {
    DWORD *row = reinterpret_cast<DWORD *>(
        static_cast<unsigned char *>(lock.pBits) + y * lock.Pitch);
    for (unsigned x = 0; x != width; ++x)
      row[x] = x < 48 && y < 24 ? 0xff000000u : 0xffffffffu;
  }
  require(source->UnlockRect(0));
}

std::vector<DWORD> readback(IDirect3DDevice9 *device, IDirect3DSurface9 *source,
                            unsigned width, unsigned height) {
  IDirect3DSurface9 *system = nullptr;
  require(device->CreateOffscreenPlainSurface(width, height, D3DFMT_A8R8G8B8,
                                              D3DPOOL_SYSTEMMEM, &system, nullptr));
  require(device->GetRenderTargetData(source, system));
  D3DLOCKED_RECT lock{};
  require(system->LockRect(&lock, nullptr, D3DLOCK_READONLY));
  std::vector<DWORD> pixels(width * height);
  for (unsigned y = 0; y != height; ++y)
    std::memcpy(pixels.data() + y * width,
                static_cast<const unsigned char *>(lock.pBits) + y * lock.Pitch,
                width * sizeof(DWORD));
  require(system->UnlockRect());
  system->Release();
  return pixels;
}

void setDrawState(IDirect3DDevice9 *device, IDirect3DSurface9 *target,
                  IDirect3DTexture9 *source, unsigned width, unsigned height) {
  require(device->SetRenderTarget(0, target));
  require(device->SetTexture(0, source));
  require(device->SetFVF(0x4c4));
  require(device->SetPixelShader(nullptr));
  require(device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE));
  require(device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE));
  require(device->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE));
  require(device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE));
  require(device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE));
  require(device->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE));
  require(device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE));
  require(device->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE));
  require(device->SetRenderState(D3DRS_ZENABLE, FALSE));
  require(device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE));
  require(device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT));
  require(device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT));
  require(device->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE));
  require(device->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, FALSE));
  D3DVIEWPORT9 viewport{0, 0, width, height, 0.0f, 1.0f};
  require(device->SetViewport(&viewport));
  require(device->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.0f, 0));
}

void readPointSamplerStates(IDirect3DDevice9 *device, DWORD values[4]) {
  const D3DSAMPLERSTATETYPE states[4] = {D3DSAMP_MAGFILTER, D3DSAMP_MINFILTER,
                                          D3DSAMP_MIPFILTER, D3DSAMP_MAXANISOTROPY};
  for (unsigned i = 0; i != 4; ++i) require(device->GetSamplerState(0, states[i], &values[i]));
}

void requirePointSamplerStates(IDirect3DDevice9 *device, const DWORD expected[4]) {
  DWORD actual[4]{};
  readPointSamplerStates(device, actual);
  assert(std::memcmp(actual, expected, sizeof(actual)) == 0);
}

void testPointSampler(IDirect3DDevice9 *device) {
  require(device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR));
  require(device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR));
  require(device->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR));
  // Nearest overrides must restore higher AF levels as well as the old 4x.
  for (DWORD level : {4u, 8u, 16u}) {
    require(device->SetSamplerState(0, D3DSAMP_MAXANISOTROPY, level));
    DWORD nativeAniso[4]{};
    readPointSamplerStates(device, nativeAniso); // Compare device-observed values, not requests.
    {
      PointSampler scope(device, true);
      const DWORD point[4] = {D3DTEXF_POINT, D3DTEXF_POINT, D3DTEXF_POINT, 1};
      requirePointSamplerStates(device, point);
    }
    requirePointSamplerStates(device, nativeAniso);
    assert(nativeAniso[3] == level);
  }

  require(device->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE));
  DWORD nativeNoMip[4]{};
  readPointSamplerStates(device, nativeNoMip);
  {
    PointSampler scope(device, true);
    const DWORD pointNoMip[4] = {D3DTEXF_POINT, D3DTEXF_POINT, D3DTEXF_NONE, 1};
    requirePointSamplerStates(device, pointNoMip);
  }
  requirePointSamplerStates(device, nativeNoMip);

  PointSampler disabled(device, false);
  requirePointSamplerStates(device, nativeNoMip);
}

void requireDrawState(IDirect3DDevice9 *device, IDirect3DSurface9 *target,
                      IDirect3DTexture9 *source, unsigned width, unsigned height) {
  IDirect3DSurface9 *actualTarget = nullptr;
  IDirect3DBaseTexture9 *actualTexture = nullptr;
  require(device->GetRenderTarget(0, &actualTarget));
  require(device->GetTexture(0, &actualTexture));
  assert(actualTarget == target && actualTexture == source);
  actualTarget->Release();
  actualTexture->Release();
  DWORD fvf = 0;
  require(device->GetFVF(&fvf));
  assert(fvf == 0x4c4u);
  D3DVIEWPORT9 viewport{};
  require(device->GetViewport(&viewport));
  assert(viewport.X == 0 && viewport.Y == 0 && viewport.Width == width &&
         viewport.Height == height && viewport.MinZ == 0.0f && viewport.MaxZ == 1.0f);
  DWORD value = 0;
  require(device->GetTextureStageState(0, D3DTSS_COLOROP, &value));
  assert(value == D3DTOP_MODULATE);
  require(device->GetTextureStageState(0, D3DTSS_COLORARG1, &value));
  assert(value == D3DTA_TEXTURE);
  require(device->GetTextureStageState(0, D3DTSS_COLORARG2, &value));
  assert(value == D3DTA_DIFFUSE);
  require(device->GetTextureStageState(0, D3DTSS_ALPHAOP, &value));
  assert(value == D3DTOP_MODULATE);
  require(device->GetTextureStageState(0, D3DTSS_ALPHAARG1, &value));
  assert(value == D3DTA_TEXTURE);
  require(device->GetTextureStageState(0, D3DTSS_ALPHAARG2, &value));
  assert(value == D3DTA_DIFFUSE);
  require(device->GetTextureStageState(1, D3DTSS_COLOROP, &value));
  assert(value == D3DTOP_DISABLE);
  require(device->GetTextureStageState(1, D3DTSS_ALPHAOP, &value));
  assert(value == D3DTOP_DISABLE);
}

void requireRestored(IDirect3DDevice9 *device, const float constants[8]) {
  IDirect3DPixelShader9 *shader = reinterpret_cast<IDirect3DPixelShader9 *>(1);
  require(device->GetPixelShader(&shader));
  assert(shader == nullptr);
  float actual[8]{};
  require(device->GetPixelShaderConstantF(0, actual, 2));
  assert(std::memcmp(actual, constants, sizeof(actual)) == 0);
  DWORD value = 0;
  require(device->GetSamplerState(0, D3DSAMP_MAGFILTER, &value));
  assert(value == D3DTEXF_POINT);
  require(device->GetSamplerState(0, D3DSAMP_MINFILTER, &value));
  assert(value == D3DTEXF_POINT);
  require(device->GetSamplerState(0, D3DSAMP_MIPFILTER, &value));
  assert(value == D3DTEXF_NONE);
}
} // namespace

int main() {
  WNDCLASSA windowClass{};
  windowClass.lpfnWndProc = DefWindowProcA;
  windowClass.hInstance = GetModuleHandleA(nullptr);
  windowClass.lpszClassName = "GuruminModernAAFixture";
  assert(RegisterClassA(&windowClass));
  HWND window = CreateWindowA(windowClass.lpszClassName, "", WS_OVERLAPPEDWINDOW,
                              0, 0, 64, 64, nullptr, nullptr,
                              windowClass.hInstance, nullptr);
  assert(window);

  IDirect3D9 *d3d = Direct3DCreate9(D3D_SDK_VERSION);
  assert(d3d);
  D3DPRESENT_PARAMETERS present{};
  present.Windowed = TRUE;
  present.SwapEffect = D3DSWAPEFFECT_DISCARD;
  present.BackBufferFormat = D3DFMT_A8R8G8B8;
  present.BackBufferWidth = 48;
  present.BackBufferHeight = 24;
  present.hDeviceWindow = window;
  IDirect3DDevice9 *device = nullptr;
  require(d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window,
                            D3DCREATE_SOFTWARE_VERTEXPROCESSING, &present, &device));
  testPointSampler(device);

  constexpr unsigned sourceW = 64, sourceH = 64, outputW = 48, outputH = 24;
  IDirect3DTexture9 *source = nullptr, *rawTexture = nullptr, *lowTexture = nullptr,
                    *highTexture = nullptr;
  require(device->CreateTexture(sourceW, sourceH, 1, 0, D3DFMT_A8R8G8B8,
                                D3DPOOL_MANAGED, &source, nullptr));
  require(device->CreateTexture(outputW, outputH, 1, D3DUSAGE_RENDERTARGET,
                                D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &rawTexture, nullptr));
  require(device->CreateTexture(outputW, outputH, 1, D3DUSAGE_RENDERTARGET,
                                D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &lowTexture, nullptr));
  require(device->CreateTexture(outputW, outputH, 1, D3DUSAGE_RENDERTARGET,
                                D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &highTexture, nullptr));
  fillDiagonal(source, sourceW, sourceH);

  IDirect3DSurface9 *raw = nullptr, *low = nullptr, *high = nullptr;
  require(rawTexture->GetSurfaceLevel(0, &raw));
  require(lowTexture->GetSurfaceLevel(0, &low));
  require(highTexture->GetSurfaceLevel(0, &high));
  const Vertex quad[4] = {
      {-0.5f, -0.5f, 0, 1, 0xffffffff, 0, 0, 0, 0, 0, 0, 0, 0, 0},
      {47.5f, -0.5f, 0, 1, 0xffffffff, 0, .75f, 0, 0, 0, 0, 0, 0, 0},
      {-0.5f, 23.5f, 0, 1, 0xffffffff, 0, 0, .375f, 0, 0, 0, 0, 0, 0},
      {47.5f, 23.5f, 0, 1, 0xffffffff, 0, .75f, .375f, 0, 0, 0, 0, 0, 0},
  };

  require(device->BeginScene());
  setDrawState(device, raw, source, outputW, outputH);
  require(nativeDraw(device, D3DPT_TRIANGLESTRIP, 2, quad, sizeof(Vertex)));
  const float sentinels[8] = {17, 18, 19, 20, 21, 22, 23, 24};
  setDrawState(device, low, source, outputW, outputH);
  require(device->SetPixelShaderConstantF(0, sentinels, 2));
  gurumin::SceneAA aa;
  calls = 0;
  require(aa.draw(device, D3DPT_TRIANGLESTRIP, 2, quad, sizeof(Vertex), 1, nativeDraw));
  assert(calls == 1 && aa.lastApplied() && SUCCEEDED(aa.lastRestoreResult()));
  requireRestored(device, sentinels);
  requireDrawState(device, low, source, outputW, outputH);
  // High uses separately compiled bytecode and must preserve the same state.
  setDrawState(device, high, source, outputW, outputH);
  require(device->SetPixelShaderConstantF(0, sentinels, 2));
  require(aa.draw(device, D3DPT_TRIANGLESTRIP, 2, quad, sizeof(Vertex), 2, nativeDraw));
  assert(calls == 2 && aa.lastApplied());
  requireRestored(device, sentinels);
  requireDrawState(device, high, source, outputW, outputH);
  require(device->EndScene());

  const std::vector<DWORD> rawPixels = readback(device, raw, outputW, outputH);
  const std::vector<DWORD> lowPixels = readback(device, low, outputW, outputH);
  const std::vector<DWORD> highPixels = readback(device, high, outputW, outputH);
  bool lowChanged = false, highChanged = false;
  bool lowCoverage = false, highCoverage = false, alphaVaries = false;
  for (size_t i = 0; i != rawPixels.size(); ++i) {
    lowChanged |= rawPixels[i] != lowPixels[i];
    highChanged |= rawPixels[i] != highPixels[i];
    assert(((rawPixels[i] >> 24) & 0xff) == ((lowPixels[i] >> 24) & 0xff));
    assert(((rawPixels[i] >> 24) & 0xff) == ((highPixels[i] >> 24) & 0xff));
    alphaVaries |= i != 0 && channel(rawPixels[i], 24) != channel(rawPixels[0], 24);
    for (DWORD candidate : {lowPixels[i], highPixels[i]}) {
      // Every FXAA result is a blend of the red/blue diagonal source, never
      // an overshoot or green-as-luma false negative.
      assert(channel(candidate, 8) == 0);
      assert(channel(candidate, 16) <= 208 && channel(candidate, 0) <= 208);
    }
    lowCoverage |= channel(lowPixels[i], 16) != 0 && channel(lowPixels[i], 0) != 0;
    highCoverage |= channel(highPixels[i], 16) != 0 && channel(highPixels[i], 0) != 0;
  }
  assert(lowChanged && highChanged && lowCoverage && highCoverage && alphaVaries);

  // HUD-like native primitives run after SceneAA and see exactly the saved
  // fixed-function state.  The covered output must match its native reference.
  const Vertex hud[4] = {
      {10, 6, 0, 1, 0xffffffff, 0, .02f, .02f, 0, 0, 0, 0, 0, 0},
      {20, 6, 0, 1, 0xffffffff, 0, .02f, .02f, 0, 0, 0, 0, 0, 0},
      {10, 12, 0, 1, 0xffffffff, 0, .02f, .02f, 0, 0, 0, 0, 0, 0},
      {20, 12, 0, 1, 0xffffffff, 0, .02f, .02f, 0, 0, 0, 0, 0, 0},
  };
  require(device->BeginScene());
  setDrawState(device, raw, source, outputW, outputH);
  require(nativeDraw(device, D3DPT_TRIANGLESTRIP, 2, quad, sizeof(Vertex)));
  require(nativeDraw(device, D3DPT_TRIANGLESTRIP, 2, hud, sizeof(Vertex)));
  setDrawState(device, low, source, outputW, outputH);
  require(aa.draw(device, D3DPT_TRIANGLESTRIP, 2, quad, sizeof(Vertex), 1, nativeDraw));
  require(nativeDraw(device, D3DPT_TRIANGLESTRIP, 2, hud, sizeof(Vertex)));
  require(device->EndScene());
  const std::vector<DWORD> rawHud = readback(device, raw, outputW, outputH);
  const std::vector<DWORD> lowHud = readback(device, low, outputW, outputH);
  for (unsigned y = 6; y < 12; ++y)
    for (unsigned x = 10; x < 20; ++x)
      assert(rawHud[y * outputW + x] == lowHud[y * outputW + x]);

  // A flat modulated native blit must remain byte-identical after FXAA's early exit.
  Vertex tinted[4];
  std::memcpy(tinted, quad, sizeof(tinted));
  for (Vertex &vertex : tinted) vertex.diffuse = 0x80c08040;
  fillFlat(source, sourceW, sourceH);
  require(device->BeginScene());
  setDrawState(device, raw, source, outputW, outputH);
  require(nativeDraw(device, D3DPT_TRIANGLESTRIP, 2, tinted, sizeof(Vertex)));
  setDrawState(device, low, source, outputW, outputH);
  require(aa.draw(device, D3DPT_TRIANGLESTRIP, 2, tinted, sizeof(Vertex), 1, nativeDraw));
  setDrawState(device, high, source, outputW, outputH);
  require(aa.draw(device, D3DPT_TRIANGLESTRIP, 2, tinted, sizeof(Vertex), 2, nativeDraw));
  require(device->EndScene());
  assert(readback(device, raw, outputW, outputH) == readback(device, low, outputW, outputH));
  assert(readback(device, raw, outputW, outputH) == readback(device, high, outputW, outputH));

  // Bright padding lies immediately beyond the UV crop.  Endpoint searches
  // must clamp to c0 rather than leak it into the valid black scene rectangle.
  fillPadding(source, sourceW, sourceH);
  require(device->BeginScene());
  setDrawState(device, raw, source, outputW, outputH);
  require(nativeDraw(device, D3DPT_TRIANGLESTRIP, 2, quad, sizeof(Vertex)));
  setDrawState(device, low, source, outputW, outputH);
  require(aa.draw(device, D3DPT_TRIANGLESTRIP, 2, quad, sizeof(Vertex), 1, nativeDraw));
  setDrawState(device, high, source, outputW, outputH);
  require(aa.draw(device, D3DPT_TRIANGLESTRIP, 2, quad, sizeof(Vertex), 2, nativeDraw));
  require(device->EndScene());
  assert(readback(device, raw, outputW, outputH) == readback(device, low, outputW, outputH));
  assert(readback(device, raw, outputW, outputH) == readback(device, high, outputW, outputH));

  // A same-size UV rectangle shifted outside the allocation cannot enable AA.
  Vertex outside[4];
  std::memcpy(outside, quad, sizeof(outside));
  for (Vertex &vertex : outside) vertex.u0 -= 0.25f;
  require(device->BeginScene());
  setDrawState(device, high, source, outputW, outputH);
  calls = 0;
  require(aa.draw(device, D3DPT_TRIANGLESTRIP, 2, outside, sizeof(Vertex), 1, nativeDraw));
  assert(calls == 1 && !aa.lastApplied());
  require(device->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, TRUE));
  calls = 0;
  require(aa.draw(device, D3DPT_TRIANGLESTRIP, 2, quad, sizeof(Vertex), 1, nativeDraw));
  assert(calls == 1 && !aa.lastApplied()); // Linear-decoded texture is native fallback.
  require(device->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, FALSE));
  require(device->EndScene());
  aa.reset();

  high->Release(); low->Release(); raw->Release();
  highTexture->Release(); lowTexture->Release(); rawTexture->Release();
  source->Release(); device->Release(); d3d->Release(); DestroyWindow(window);
  UnregisterClassA(windowClass.lpszClassName, windowClass.hInstance);
}
