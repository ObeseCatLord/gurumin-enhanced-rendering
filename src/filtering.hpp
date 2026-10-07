#pragma once
#include <d3d9.h>

// Primitive-local state transaction. Native cached sampler policy is untouched.
class PointSampler {
  IDirect3DDevice9 *device;
  DWORD saved[4]{};
  bool captured=false;
  static constexpr D3DSAMPLERSTATETYPE states[4]={D3DSAMP_MAGFILTER,
    D3DSAMP_MINFILTER,D3DSAMP_MIPFILTER,D3DSAMP_MAXANISOTROPY};
public:
  PointSampler(IDirect3DDevice9 *d,bool apply):device(d) {
    if(!apply) return;
    for(unsigned i=0;i<4;++i)
      if(FAILED(d->GetSamplerState(0,states[i],&saved[i]))) return;
    captured=true;
    DWORD point[4]={D3DTEXF_POINT,D3DTEXF_POINT,
      saved[2]==D3DTEXF_NONE?D3DTEXF_NONE:D3DTEXF_POINT,1};
    for(unsigned i=0;i<4;++i)
      if(FAILED(d->SetSamplerState(0,states[i],point[i]))) {
        restore(); return;
      }
  }
  void restore() {
    if(captured) {
      for(unsigned i=0;i<4;++i) device->SetSamplerState(0,states[i],saved[i]);
      captured=false;
    }
  }
  ~PointSampler() { restore(); }
  PointSampler(const PointSampler&)=delete;
  PointSampler& operator=(const PointSampler&)=delete;
};
