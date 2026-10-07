// FXAA 3.11 Quality PC integration.  The NVIDIA implementation is vendored in
// ../vendor/fxaa/Fxaa3_11.h; this file supplies the D3D9 scene-copy contract.
// c0 = (minimumU, minimumV, maximumU, maximumV), all texel centres.
// c1 = (1 / allocationWidth, 1 / allocationHeight, 0, 0).
sampler2D sceneTexture : register(s0);
float4 sampleBounds : register(c0);
float4 texelSize : register(c1);

float2 boundedUv(float2 uv) {
  return clamp(uv, sampleBounds.xy, sampleBounds.zw);
}

// This is selected by the narrowly adapted FxaaTexTop/FxaaTexOff helpers.
// FXAA receives perceptual luma in .a, while output alpha remains the source
// alpha sampled at the original pixel centre.
float4 FxaaBoundedRgbLuma(sampler2D textureSampler, float2 uv) {
  float4 source = tex2Dlod(textureSampler, float4(boundedUv(uv), 0.0, 0.0));
  return float4(source.rgb, dot(source.rgb, float3(0.299, 0.587, 0.114)));
}

#include "../vendor/fxaa/Fxaa3_11.h"

float4 edgeAA(float2 uv, float4 diffuse, float subpix, float edgeThreshold) {
  float4 originalCenter = tex2Dlod(sceneTexture, float4(boundedUv(uv), 0.0, 0.0));
  float4 filtered = FxaaPixelShader(
      uv, 0.0, sceneTexture, sceneTexture, sceneTexture, sceneTexture,
      texelSize.xy, 0.0, 0.0, 0.0,
      subpix, edgeThreshold, 0.0312,
      0.0, 0.0, 0.0, 0.0);
  return float4(filtered.rgb * diffuse.rgb, originalCenter.a * diffuse.a);
}

float4 EdgeAALow(float2 uv : TEXCOORD0, float4 diffuse : COLOR0) : COLOR0 {
  return edgeAA(uv, diffuse, 0.50, 0.125);
}

float4 EdgeAAHigh(float2 uv : TEXCOORD0, float4 diffuse : COLOR0) : COLOR0 {
  return edgeAA(uv, diffuse, 0.75, 0.063);
}
