#pragma once

#include <cmath>
#include <cstring>
#include <d3d9.h>
#include "edge_aa_bytecode.hpp"

namespace gurumin {

// Replaces only the verified ordinary scene-texture blit pixel shader.  The
// caller owns the native return-address, callback, scene-texture, and menu
// gates; this class validates the remaining D3D state and otherwise draws natively.
class SceneAA {
public:
  using DrawUP = HRESULT(WINAPI *)(IDirect3DDevice9 *, D3DPRIMITIVETYPE, UINT,
                                   const void *, UINT);

  SceneAA() = default;
  SceneAA(const SceneAA &) = delete;
  SceneAA &operator=(const SceneAA &) = delete;
  ~SceneAA() { reset(); }

  void reset() {
    if (low_) low_->Release();
    if (high_) high_->Release();
    low_ = high_ = nullptr;
    lastApplied_ = false;
    lastRestore_ = S_OK;
  }

  bool lastApplied() const { return lastApplied_; }
  HRESULT lastRestoreResult() const { return lastRestore_; }

  HRESULT draw(IDirect3DDevice9 *device, D3DPRIMITIVETYPE type, UINT count,
               const void *vertices, UINT stride, int quality,
               DrawUP nativeDraw) {
    lastApplied_ = false;
    lastRestore_ = S_OK;
    if (!nativeDraw) return D3DERR_INVALIDCALL;
    const auto native = [&] { return nativeDraw(device, type, count, vertices, stride); };
    if (!device || quality < 1 || quality > 2 || type != D3DPT_TRIANGLESTRIP ||
        count != 2 || !vertices || stride != 56)
      return native();

    DWORD fvf = 0;
    if (FAILED(device->GetFVF(&fvf)) || fvf != 0x4c4u) return native();

    IDirect3DPixelShader9 *originalShader = nullptr;
    if (FAILED(device->GetPixelShader(&originalShader))) return native();
    // The approved branch is the native ordinary NULL-PS path.  A non-null
    // effect shader is not replaced, even if the geometry happens to match.
    if (originalShader) {
      originalShader->Release();
      return native();
    }

    IDirect3DBaseTexture9 *baseTexture = nullptr;
    D3DSURFACE_DESC sceneDesc{};
    if (FAILED(device->GetTexture(0, &baseTexture)) || !baseTexture ||
        baseTexture->GetType() != D3DRTYPE_TEXTURE ||
        FAILED(static_cast<IDirect3DTexture9 *>(baseTexture)->GetLevelDesc(0, &sceneDesc)) ||
        !sceneDesc.Width || !sceneDesc.Height) {
      if (baseTexture) baseTexture->Release();
      return native();
    }
    baseTexture->Release();

    // FXAA's synthetic perceptual luma assumes the native scene texture is
    // sampled as encoded, non-linear RGB.  D3D9 sRGB decode would return
    // linear values, so preserve the verified native blit on that branch.
    DWORD srgbDecode = FALSE;
    if (FAILED(device->GetSamplerState(0, D3DSAMP_SRGBTEXTURE, &srgbDecode)) ||
        srgbDecode != FALSE)
      return native();

    float bounds[4];
    if (!deriveBounds(vertices, stride, sceneDesc.Width, sceneDesc.Height, bounds) ||
        !matchesFixedFunctionBlit(device, vertices, stride))
      return native();
    D3DVIEWPORT9 viewport{};
    if (FAILED(device->GetViewport(&viewport)) || !viewport.Width || !viewport.Height ||
        !matchesViewport(bounds, sceneDesc, viewport))
      return native();

    IDirect3DPixelShader9 *shader = quality == 1 ? low_ : high_;
    if (!shader) {
      const DWORD *code = reinterpret_cast<const DWORD *>(quality == 1
          ? edge_aa_bytecode::low : edge_aa_bytecode::high);
      const unsigned size = quality == 1 ? edge_aa_bytecode::lowSize
                                         : edge_aa_bytecode::highSize;
      if (!size || FAILED(device->CreatePixelShader(code, &shader)) || !shader)
        return native();
      if (quality == 1) low_ = shader;
      else high_ = shader;
    }

    SavedState saved{};
    if (FAILED(device->GetPixelShaderConstantF(0, saved.constants, 2)) ||
        FAILED(device->GetSamplerState(0, D3DSAMP_MAGFILTER, &saved.mag)) ||
        FAILED(device->GetSamplerState(0, D3DSAMP_MINFILTER, &saved.min)) ||
        FAILED(device->GetSamplerState(0, D3DSAMP_MIPFILTER, &saved.mip)))
      return native();

    const float constants[8] = {bounds[0], bounds[1], bounds[2], bounds[3],
                                1.0f / sceneDesc.Width, 1.0f / sceneDesc.Height,
                                0.0f, 0.0f};
    bool shaderTouched = true, constantsTouched = false;
    unsigned samplerTouched = 0;
    HRESULT prep = device->SetPixelShader(shader);
    if (SUCCEEDED(prep)) {
      constantsTouched = true;
      prep = device->SetPixelShaderConstantF(0, constants, 2);
    }
    if (SUCCEEDED(prep)) {
      samplerTouched = 1;
      prep = device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    }
    if (SUCCEEDED(prep)) {
      samplerTouched = 2;
      prep = device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    }
    if (SUCCEEDED(prep)) {
      samplerTouched = 3;
      prep = device->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    }

    if (FAILED(prep)) {
      restore(device, saved, shaderTouched, constantsTouched, samplerTouched);
      return native();
    }

    // NativeDraw is invoked exactly once in every path.  It is the original
    // DrawPrimitiveUP and does not own shader state.
    lastApplied_ = true;
    const HRESULT result = native();
    restore(device, saved, true, true, 3);
    return result;
  }

private:
  struct SavedState {
    float constants[8]{};
    DWORD mag = 0, min = 0, mip = 0;
  };

  static bool finite(float value) { return std::isfinite(value) != 0; }

  static bool deriveBounds(const void *vertices, UINT stride, UINT width, UINT height,
                           float out[4]) {
    const unsigned char *data = static_cast<const unsigned char *>(vertices);
    float minimumU = 1.0e30f, minimumV = 1.0e30f;
    float maximumU = -1.0e30f, maximumV = -1.0e30f;
    for (unsigned i = 0; i != 4; ++i) {
      float uv[2];
      // FVF 0x4C4: XYZRHW (16), diffuse/specular (8), TEXCOORD0 at 24/28.
      std::memcpy(uv, data + i * stride + 24, sizeof(uv));
      if (!finite(uv[0]) || !finite(uv[1])) return false;
      if (uv[0] < minimumU) minimumU = uv[0];
      if (uv[1] < minimumV) minimumV = uv[1];
      if (uv[0] > maximumU) maximumU = uv[0];
      if (uv[1] > maximumV) maximumV = uv[1];
    }
    const float halfU = 0.5f / width, halfV = 0.5f / height;
    out[0] = minimumU + halfU;
    out[1] = minimumV + halfV;
    out[2] = maximumU - halfU;
    out[3] = maximumV - halfV;
    return minimumU >= 0.0f && minimumV >= 0.0f && maximumU <= 1.0f &&
           maximumV <= 1.0f && finite(out[0]) && finite(out[1]) &&
           finite(out[2]) && finite(out[3]) && out[0] >= halfU && out[1] >= halfV &&
           out[2] <= 1.0f - halfU && out[3] <= 1.0f - halfV &&
           out[0] <= out[2] && out[1] <= out[3];
  }

  static bool isTextureDiffuse(DWORD first, DWORD second) {
    return (first == D3DTA_TEXTURE && second == D3DTA_DIFFUSE) ||
           (first == D3DTA_DIFFUSE && second == D3DTA_TEXTURE);
  }

  static bool allDiffuseWhite(const void *vertices, UINT stride) {
    const unsigned char *data = static_cast<const unsigned char *>(vertices);
    for (unsigned i = 0; i != 4; ++i) {
      DWORD diffuse = 0;
      std::memcpy(&diffuse, data + i * stride + 16, sizeof(diffuse));
      if (diffuse != 0xffffffffu) return false;
    }
    return true;
  }

  static bool matchesFixedFunctionBlit(IDirect3DDevice9 *device,
                                       const void *vertices, UINT stride) {
    DWORD colorOp = 0, colorArg1 = 0, colorArg2 = 0;
    DWORD alphaOp = 0, alphaArg1 = 0, alphaArg2 = 0;
    DWORD stage1Color = 0, stage1Alpha = 0, coordinate = 0, transform = 0;
    if (FAILED(device->GetTextureStageState(0, D3DTSS_COLOROP, &colorOp)) ||
        FAILED(device->GetTextureStageState(0, D3DTSS_COLORARG1, &colorArg1)) ||
        FAILED(device->GetTextureStageState(0, D3DTSS_COLORARG2, &colorArg2)) ||
        FAILED(device->GetTextureStageState(0, D3DTSS_ALPHAOP, &alphaOp)) ||
        FAILED(device->GetTextureStageState(0, D3DTSS_ALPHAARG1, &alphaArg1)) ||
        FAILED(device->GetTextureStageState(0, D3DTSS_ALPHAARG2, &alphaArg2)) ||
        FAILED(device->GetTextureStageState(0, D3DTSS_TEXCOORDINDEX, &coordinate)) ||
        FAILED(device->GetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, &transform)) ||
        FAILED(device->GetTextureStageState(1, D3DTSS_COLOROP, &stage1Color)) ||
        FAILED(device->GetTextureStageState(1, D3DTSS_ALPHAOP, &stage1Alpha)))
      return false;
    if (coordinate != 0 || transform != D3DTTFF_DISABLE ||
        stage1Color != D3DTOP_DISABLE || stage1Alpha != D3DTOP_DISABLE)
      return false;
    const bool modulate = colorOp == D3DTOP_MODULATE && alphaOp == D3DTOP_MODULATE &&
                          isTextureDiffuse(colorArg1, colorArg2) &&
                          isTextureDiffuse(alphaArg1, alphaArg2);
    if (modulate) return true;
    return colorOp == D3DTOP_SELECTARG1 && colorArg1 == D3DTA_TEXTURE &&
           alphaOp == D3DTOP_SELECTARG1 && alphaArg1 == D3DTA_TEXTURE &&
           allDiffuseWhite(vertices, stride);
  }

  static bool matchesViewport(const float bounds[4], const D3DSURFACE_DESC &desc,
                              const D3DVIEWPORT9 &viewport) {
    const float sampledWidth = (bounds[2] - bounds[0]) * desc.Width + 1.0f;
    const float sampledHeight = (bounds[3] - bounds[1]) * desc.Height + 1.0f;
    return std::fabs(sampledWidth - viewport.Width) <= 1.1f &&
           std::fabs(sampledHeight - viewport.Height) <= 1.1f;
  }

  void restore(IDirect3DDevice9 *device, const SavedState &saved, bool shaderSet,
               bool constantsSet, unsigned samplerSet) {
    HRESULT failure = S_OK;
    const auto note = [&](HRESULT result) {
      if (SUCCEEDED(failure) && FAILED(result)) failure = result;
    };
    if (samplerSet >= 3) note(device->SetSamplerState(0, D3DSAMP_MIPFILTER, saved.mip));
    if (samplerSet >= 2) note(device->SetSamplerState(0, D3DSAMP_MINFILTER, saved.min));
    if (samplerSet >= 1) note(device->SetSamplerState(0, D3DSAMP_MAGFILTER, saved.mag));
    if (constantsSet) note(device->SetPixelShaderConstantF(0, saved.constants, 2));
    if (shaderSet) note(device->SetPixelShader(nullptr));
    lastRestore_ = failure;
  }

  IDirect3DPixelShader9 *low_ = nullptr;
  IDirect3DPixelShader9 *high_ = nullptr;
  bool lastApplied_ = false;
  HRESULT lastRestore_ = S_OK;
};

} // namespace gurumin
