// Exercises the actual proxy pairing code in an isolated Windows process.
#include "../src/display.cpp"
#include <cassert>
static void identity(float *m, float x = 0) {
  std::fill(m, m + 16, 0.f);
  m[0] = m[5] = m[10] = m[15] = 1;
  m[12] = x;
}
static void pack(const float *m, float *p) {
  for (int r = 0; r < 3; ++r)
    for (int c = 0; c < 4; ++c)
      p[r * 4 + c] = m[c * 4 + r];
}
static unsigned __thiscall verifyNativeDraw(void *, const float *world,
                                            const float *view, unsigned mode,
                                            unsigned, const void *,
                                            const void *) {
  assert(drawArgumentsFiltered);
  assert(mode == 8 || mode == 15 || mode == 30 || mode == 31 || mode == 33);
  assert(worldShaderDepth == 1 && game<int>(0x508b14) == 3440 &&
         game<int>(0x508b18) == 1440);
  assert(std::abs(world[12] - 7.f) < 1e-5);
  assert(view[0] == 1 && view[5] == 1 && view[11] == 1 && view[14] == -1);
  assert(!memcmp(view,cameraRendered,64));
  return 123;
}
static char *casterRoot,*casterChild;
static float casterNative[12],casterSubmitted[12],unchangedCamera[32];
static unsigned unchangedCameraFrame;
static bool expectCasterScope,expectCasterFiltering;
static unsigned __thiscall verifyCasterDraw(void *,const float *world,
    const float *view,unsigned mode,unsigned a,const void *b,const void *c) {
  assert(mode==16 && a==0xabcd && b==reinterpret_cast<void*>(0x5678) && c==reinterpret_cast<void*>(0x9999));
  assert(drawArgumentsFiltered==expectCasterFiltering);
  assert(std::abs(world[12]-(expectCasterFiltering?7.f:12.f))<1e-5);
  assert(!memcmp(view,modelView,64)); // Light VP is never smoothed.
  assert(cameraFrame==unchangedCameraFrame && !memcmp(cameraNative,unchangedCamera,64) &&
         !memcmp(cameraRendered,unchangedCamera+16,64));
  return 0xfedcba00u;
}
static unsigned __thiscall verifyCasterModel(void *obj,const float *parent,const float *view,
    unsigned a,unsigned b,const void *c,const void *e) {
  assert(modelOwner==obj && modelView==view && parent==reinterpret_cast<float*>(0x1234));
  assert(a==0xabcd && b==0xffffffff && c==reinterpret_cast<void*>(0x5678) && e==reinterpret_cast<void*>(0x9999));
  assert(shadowCasterDepth==(expectCasterScope?(obj==casterRoot?1:2):0));
  if(obj==casterRoot) {
    rememberSubmittedPalette(casterNative,casterSubmitted,3);
    return modelDrawHook(casterChild,nullptr,parent,view,a,b,c,e);
  }
  return shaderDrawHook(nullptr,nullptr,reinterpret_cast<float*>(casterChild+0x24c),
      view,16,a,c,e);
}
static bool expectParticleScope;
static const float *expectedAxes;
static bool expectedCameraScope;
static bool expectedFixedCamera, fakeCameraOutputs, fakeAuthoredCamera;
static SHORT testRightX, testRightY;
static float authoredShift;
static void __cdecl verifyInput(int *) {}
static DWORD WINAPI verifyXInput(DWORD device, XINPUT_STATE *state) {
  assert(device==0); *state={};
  state->Gamepad.sThumbRX=testRightX; state->Gamepad.sThumbRY=testRightY;
  return ERROR_SUCCESS;
}
static void *expectedGeometry;
static unsigned segmentCalls;
static float capturedQuad[32];
static IDirect3DBaseTexture9 *fakeAsset;
static DWORD fakeAssetUsage;
static ULONG WINAPI fakeRelease(IDirect3DBaseTexture9*) { return 1; }
static D3DRESOURCETYPE WINAPI fakeTextureType(IDirect3DBaseTexture9*) { return D3DRTYPE_TEXTURE; }
static HRESULT WINAPI fakeTextureDesc(IDirect3DTexture9*,UINT,D3DSURFACE_DESC *desc) {
  *desc={}; desc->Width=256; desc->Height=256; desc->Usage=fakeAssetUsage;
  return S_OK;
}
static HRESULT WINAPI fakeGetTexture(IDirect3DDevice9*,DWORD stage,IDirect3DBaseTexture9 **out) {
  assert(stage==0); *out=fakeAsset; return S_OK;
}
static HRESULT WINAPI fakeFVF(IDirect3DDevice9*,DWORD *out) { *out=0x1c4; return S_OK; }
static HRESULT WINAPI fakeViewport(IDirect3DDevice9*,D3DVIEWPORT9 *out) {
  *out=actualViewport; return S_OK;
}
static HRESULT WINAPI captureQuad(IDirect3DDevice9*,D3DPRIMITIVETYPE t,UINT n,
                                  const void *data,UINT stride) {
  assert(t==D3DPT_TRIANGLESTRIP && n==2 && stride==32);
  memcpy(capturedQuad,data,sizeof(capturedQuad)); return S_OK;
}
static IDirect3DDevice9 *rectangleDevice;
static unsigned __cdecl verifySolidRect(int x0,int y0,int x1,int y1,
    unsigned color,float z,float alpha) {
  assert(std::abs(z-.7f)<1e-6 && alpha==.5f);
  float vertices[32]{};
  for(unsigned i=0;i<4;++i) {
    auto v=vertices+i*8;
    v[0]=float(i%2?x1:x0)*game<int>(0x508b14)/640.f;
    v[1]=float(i/2?y1:y0)*game<int>(0x508b18)/480.f;
    v[2]=z;v[3]=1;
    memcpy(v+4,&color,4);
  }
  drawHook(rectangleDevice,D3DPT_TRIANGLESTRIP,2,vertices,32);
  return 0xabcd5678u;
}
static UINT expectedPresentation;
static HRESULT WINAPI verifyDeviceCreation(IDirect3D9*,UINT,D3DDEVTYPE,HWND,
    DWORD,D3DPRESENT_PARAMETERS *params,IDirect3DDevice9 **out) {
  assert(params->PresentationInterval==expectedPresentation); *out=nullptr;
  return D3DERR_INVALIDCALL;
}
static HRESULT WINAPI verifyDeviceReset(IDirect3DDevice9*,D3DPRESENT_PARAMETERS *params) {
  assert(params->PresentationInterval==expectedPresentation); return D3DERR_INVALIDCALL;
}
static float capturedPitch;
static unsigned __cdecl verifyCameraUpdate(float *eye, float *target) {
  assert(eye && target && cameraPitchScope == expectedCameraScope);
  if(expectedFixedCamera) assert(game<int>(0x1264a30)==0);
  if(fakeAuthoredCamera) {
    assert(game<int>(0x1264a30)==0);
    float e[]={30+authoredShift,700,600,1}, t[]={30+authoredShift,20,100,1};
    memcpy(reinterpret_cast<void*>(gameBase+0x1d7b5b0),e,sizeof(e));
    memcpy(reinterpret_cast<void*>(gameBase+0x1ae4900),t,sizeof(t));
    for(unsigned rva:{0x1cf17a0u,0x1dffed8u})
      memcpy(reinterpret_cast<void*>(gameBase+rva),e,sizeof(e));
    for(unsigned rva:{0x198f690u,0x1e00ee8u})
      memcpy(reinterpret_cast<void*>(gameBase+rva),t,sizeof(t));
    memcpy(eye,e,sizeof(e)); memcpy(target,t,sizeof(t));
  }
  if(fakeCameraOutputs) {
    float e[]={0,100,-100,1}, t[]={0,0,100,1};
    memcpy(eye,e,sizeof(e)); memcpy(target,t,sizeof(t));
  }
  return 77;
}
static CameraVector capturedViewEye, capturedViewTarget;
static bool expectWorldPublication, replaceWorldEye;
static unsigned worldCalls;
static float *__cdecl verifyCameraView(float *out,CameraVector eye,
    CameraVector target,float *axes,float roll) {
  assert(axes==expectedAxes && roll==.125f);
  capturedViewEye=eye; capturedViewTarget=target;
  identity(out,eye.v[0]); return out;
}
static void __cdecl verifyCameraWorld() {
  ++worldCalls;
  if(expectWorldPublication) {
    assert(!memcmp(reinterpret_cast<void*>(gameBase+0x1cf17a0),orbitRenderPose.eye.v,16));
    assert(!memcmp(reinterpret_cast<void*>(gameBase+0x198f690),orbitRenderPose.target.v,16));
    for(unsigned rva:{0x1d7b5b0u,0x1dffed8u})
      assert(!memcmp(reinterpret_cast<void*>(gameBase+rva),orbitRenderPose.nativeEye.v,16));
  }
  if(replaceWorldEye) game<float>(0x1cf17a0)=1234;
}
static unsigned movementCalls;
static unsigned __cdecl verifyCameraMovement(float *outX,float *outY,float x,float y) {
  ++movementCalls;
  // Model the native carry/input side effects and a transitional blend output.
  memcpy(reinterpret_cast<void*>(gameBase+0x1d7b5f0),
      reinterpret_cast<void*>(gameBase+0x1d7b5b0),16);
  memcpy(reinterpret_cast<void*>(gameBase+0x1d7b600),
      reinterpret_cast<void*>(gameBase+0x1ae4900),16);
  game<float>(0x1d7b640)=x; game<float>(0x1d7b644)=y;
  *outX=x==0 && y==0?-0.f:.125f;
  *outY=x==0 && y==0?0.f:-.25f;
  return 0; // This converter's normal native return is zero.
}
static float *__thiscall verifyBubbleAnchor(const float *matrix,float *out) {
  out[0]=matrix[12]; out[1]=matrix[13]; out[2]=matrix[14]; out[3]=0;
  return out;
}
static float *__cdecl verifyProjection(float *out,const float *in,const float *matrix) {
  assert(matrix==cameraRendered); (void)in;
  out[0]=.25f;out[1]=.125f;out[2]=0;out[3]=1;return out;
}
static unsigned __thiscall verifyBubblePanel(void *obj,int x,int y,int width,
    int height,float z,float alpha) {
  assert(obj==reinterpret_cast<void*>(0x1234) && width==80 && height==52);
  assert(std::abs(z-.2f)<1e-6 && std::abs(alpha-.7f)<1e-6);
  assert(std::abs(spriteXDelta-(x==0?2.f:2.f+(bubbleX-std::trunc(bubbleX))*3.f))<1e-4);
  assert(std::abs(spriteYDelta-(y==32?4.f:4.f+(bubbleY-std::trunc(bubbleY))*3.f))<1e-4);
  return 777;
}
static unsigned __thiscall verifySegment(void *geometry,const char *filter,
    unsigned mask,const float *eye,const float *target,float *hit,float *distance) {
  assert(geometry==expectedGeometry && !filter && mask==0);
  assert(*distance==999999.f);
  for(unsigned i=0;i<3;++i) hit[i]=(eye[i]+target[i])*.5f;
  hit[3]=1; *distance=111.8f;
  ++segmentCalls;
  return 1;
}
static void __thiscall verifyPitchRotation(float *vector, float angle) {
  assert(vector && vector[0] == 42.f);
  capturedPitch = angle;
}
static BOOL WINAPI verifyDialogEnd(HWND window, INT_PTR result) {
  assert(window == launcherWindow && (result == IDOK || result == IDCANCEL));
  // MFC already ends its loop before invoking the Win32 API; a false Win32
  // result must not invalidate an otherwise accepted settings transaction.
  return FALSE;
}
static void *callerThunk(unsigned returnRva, void *destination, unsigned args, bool calleeCleans=true) {
  unsigned entryRva = returnRva - 5 - args * 4;
  auto code = reinterpret_cast<unsigned char *>(gameBase + entryRva);
  DWORD old;
  assert(VirtualProtect(code, 80, PAGE_EXECUTE_READWRITE, &old));
  unsigned i = 0;
  for (unsigned a = 0; a < args; ++a) {
    code[i++] = 0xff;
    code[i++] = 0x74;
    code[i++] = 0x24;
    code[i++] = static_cast<unsigned char>(args * 4);
  }
  code[i++] = 0xe8;
  uint32_t relative = uint32_t(uintptr_t(destination) - (gameBase + returnRva));
  memcpy(code + i, &relative, 4);
  i += 4;
  if(!calleeCleans) { code[i++]=0x83; code[i++]=0xc4; code[i++]=static_cast<unsigned char>(args*4); }
  code[i++] = 0xc2;
  code[i++] = static_cast<unsigned char>(args * 4);
  code[i++] = 0;
  FlushInstructionCache(GetCurrentProcess(), code, i);
  return code;
}
static uintptr_t quadArguments[4]={64,1440,3440,0xabcdef};
static unsigned quadMode=1,quadExpectedDepth=1;
static int quadExpectedWidth=3440,quadExpectedHeight=1440;
static bool quadNested;
static unsigned __thiscall verifyProjectedQuad(void *texture,uintptr_t a,
    uintptr_t b,uintptr_t c,uintptr_t e,float opacity,unsigned mode) {
  assert(texture==reinterpret_cast<void*>(0x1234));
  assert(a==quadArguments[0] && b==quadArguments[1] && c==quadArguments[2] && e==quadArguments[3] && mode==quadMode);
  assert(std::abs(opacity-.7f)<1e-6);
  assert(projectedGeometryDepth==int(quadExpectedDepth) &&
      game<int>(0x508b14)==quadExpectedWidth && game<int>(0x508b18)==quadExpectedHeight);
  if(quadNested) {
    quadNested=false;quadExpectedDepth=2;
    assert(projectedQuadHook(texture,nullptr,a,b,c,e,opacity,mode)==789);
    quadExpectedDepth=1;
    assert(projectedGeometryDepth==1 && game<int>(0x508b14)==quadExpectedWidth);
  }
  return 789;
}
static unsigned binderResult;
static unsigned __thiscall verifyBinder(void *obj,int w,int h,int inset) {
  assert(obj==reinterpret_cast<void*>(0x1234) && w==731 && h==492 && inset==7);
  return binderResult;
}
static unsigned __thiscall verifySprite(void *obj,int a,int b,int c,int d,
    float u0,float v0,float u1,float v1,float z,float alpha,float angle,float other,int mode) {
  assert(obj==reinterpret_cast<void*>(0x1234) && a==1 && b==2 && c==3 && d==4 && mode==9);
  assert(u0==.125f && v0==.25f && u1==.375f && v1==.5f && z==.625f && alpha==.75f && angle==.875f && other==1);
  return 0xab00ff00u;
}
static unsigned __thiscall verifyReceiver(void *obj,uintptr_t a,const float *b,
    uintptr_t c,uintptr_t d,uintptr_t e,uintptr_t f,uintptr_t g,uintptr_t h,
    uintptr_t i,uintptr_t j,uintptr_t k,uintptr_t l,uintptr_t m,uintptr_t n) {
  assert(obj==reinterpret_cast<void*>(0x1234) && a==1 && b==expectedAxes);
  assert(c==3 && d==4 && e==5 && f==6 && g==7 && h==8 && i==9 && j==10 && k==11 && l==12 && m==13 && n==14);
  return 0xab00ff00u;
}
static unsigned __thiscall verifyParticleDraw(void *obj, const float *view,
                                              const float *axes,
                                              unsigned category) {
  assert(obj != nullptr && category == 0x123 && axes == expectedAxes);
  if (expectParticleScope) {
    assert(projectedGeometryDepth == 1 && game<int>(0x508b14) == 3440);
    assert(view == cameraRendered);
  } else {
    assert(projectedGeometryDepth == 0 && game<int>(0x508b14) == 1920);
    assert(view == cameraNative);
  }
  return 456;
}
int main() {
  logFile = stderr;
  std::vector<unsigned char> fakeGame(0x1e28100);
  gameBase = reinterpret_cast<uintptr_t>(fakeGame.data());
  game<unsigned>(0x1de47f4) = 1;
  QueryPerformanceFrequency(&frequency);
  char root[0x800]{}, bone[0x800]{}, child[0x800]{}, other[0x800]{};
  std::vector<char> controller(0x5620);
  *reinterpret_cast<char **>(controller.data() + 0x1108) = bone;
  *reinterpret_cast<char **>(bone + 0x74c) = root;
  *reinterpret_cast<char **>(child + 0x74c) = bone;
  float view[16], nativeMatrix[16], submittedMatrix[16], childMatrix[16];
  float nativePacked[12], submittedPacked[12], childPacked[12], result[12];
  identity(view);
  modelView = view;
  frames = 1;
  currentSkin = {reinterpret_cast<float *>(controller.data() + 0x4618),
                 reinterpret_cast<float *>(root + 0x24c), 1};
  identity(nativeMatrix, 10);
  identity(submittedMatrix, 5);
  identity(childMatrix, 12);
  pack(nativeMatrix, nativePacked);
  pack(submittedMatrix, submittedPacked);
  pack(childMatrix, childPacked);
  modelOwner = root;
  rememberSubmittedPalette(nativePacked, submittedPacked, 3);
  modelOwner = child;
  drawingModel = reinterpret_cast<float *>(child + 0x24c);
  assert(smoothAttachment(childPacked, result));
  assert(std::abs(result[3] - 7) < 1e-5);
  // Fallback submission follows native data even if an interpolation cache
  // exists.
  modelOwner = root;
  rememberSubmittedPalette(nativePacked, nativePacked, 3);
  renderPoses[currentSkin].result.assign(12, 99.f);
  renderPoses[currentSkin].frame = frames;
  modelOwner = child;
  assert(smoothAttachment(childPacked, result));
  assert(std::abs(result[3] - 12) < 1e-5);
  // Same-tick source changes use the new actual native submission, not cached
  // endpoints.
  identity(nativeMatrix, 20);
  identity(childMatrix, 22);
  pack(nativeMatrix, nativePacked);
  pack(childMatrix, childPacked);
  modelOwner = root;
  rememberSubmittedPalette(nativePacked, submittedPacked, 3);
  modelOwner = child;
  assert(smoothAttachment(childPacked, result));
  assert(std::abs(result[3] - 7) < 1e-5);
  // Equipment replacement can release the submitted controller in this
  // frame. Bone identities are captured at submission, not re-read later.
  auto &capturedBody=skinBodies[root];
  auto savedBones=capturedBody.key.bones;
  capturedBody.key.bones=reinterpret_cast<float*>(0xdeadbeef);
  modelOwner=child; assert(smoothAttachment(childPacked,result));
  assert(std::abs(result[3]-7)<1e-5);
  capturedBody.key.bones=savedBones;
  char *savedParent=*reinterpret_cast<char**>(child+0x74c);
  *reinterpret_cast<char**>(child+0x74c)=reinterpret_cast<char*>(0xdeadbeef);
  assert(!smoothAttachment(childPacked,result));
  *reinterpret_cast<char**>(child+0x74c)=savedParent;
  // Reject malformed readable hierarchies, including cycles that exclude
  // the child and a chain still non-null after 64 nodes.
  *reinterpret_cast<char**>(root+0x74c)=other;
  *reinterpret_cast<char**>(other+0x74c)=root;
  assert(!smoothAttachment(childPacked,result));
  *reinterpret_cast<char**>(root+0x74c)=nullptr;
  *reinterpret_cast<char**>(other+0x74c)=nullptr;
  std::vector<std::vector<char>> chain(64,std::vector<char>(0x800));
  *reinterpret_cast<char**>(root+0x74c)=chain[0].data();
  for(unsigned i=0;i+1<chain.size();++i)
    *reinterpret_cast<char**>(chain[i].data()+0x74c)=chain[i+1].data();
  assert(!smoothAttachment(childPacked,result));
  *reinterpret_cast<char**>(root+0x74c)=nullptr;
  // Same-frame reparenting cannot reuse the former parent's palette.
  *reinterpret_cast<char**>(child+0x74c)=other;
  assert(!smoothAttachment(childPacked,result));
  *reinterpret_cast<char**>(child+0x74c)=savedParent;
  // Unreadable current owner mapping simply excludes attachment pairing.
  auto savedSkin=currentSkin;modelOwner=root;
  currentSkin.bones=reinterpret_cast<float*>(0xdeadbeef);
  rememberSubmittedPalette(nativePacked,submittedPacked,3);
  modelOwner=child; assert(!smoothAttachment(childPacked,result));
  currentSkin=savedSkin;modelOwner=root;
  rememberSubmittedPalette(nativePacked,submittedPacked,3);modelOwner=child;
  // A same-address model replacement must refresh owner identities and
  // actual palette data in this frame, instead of pairing the former body.
  auto ownerSlot=reinterpret_cast<const char**>(controller.data()+0x1108);
  *ownerSlot=other;modelOwner=root;
  rememberSubmittedPalette(nativePacked,submittedPacked,3);modelOwner=child;
  assert(!smoothAttachment(childPacked,result));
  *ownerSlot=bone;modelOwner=root;
  float replacementNative[16],replacementSubmitted[16],replacementChild[16];
  float rn[12],rs[12],rc[12];
  identity(replacementNative,30);identity(replacementSubmitted,15);identity(replacementChild,32);
  pack(replacementNative,rn);pack(replacementSubmitted,rs);pack(replacementChild,rc);
  rememberSubmittedPalette(rn,rs,3);modelOwner=child;
  assert(smoothAttachment(rc,result) && std::abs(result[3]-17)<1e-5);
  modelOwner=root;rememberSubmittedPalette(nativePacked,submittedPacked,3);modelOwner=child;
  // Competing instances sharing a controller cannot be selected by map
  // iteration.
  modelOwner = other;
  currentSkin.model = reinterpret_cast<float *>(other + 0x24c);
  rememberSubmittedPalette(nativePacked, submittedPacked, 3);
  modelOwner = child;
  assert(!smoothAttachment(childPacked, result));
  // A different pass is excluded, leaving the unique scene-body submission.
  skinBodies[other].view[12] = 50;
  assert(smoothAttachment(childPacked, result));
  assert(std::abs(result[3] - 7) < 1e-5);
  // Previous-frame snapshots do not authorize a current attachment.
  ++frames;
  assert(!smoothAttachment(childPacked, result));
  // Verify real shader entry correction for every uncovered native branch.
  float *projection = reinterpret_cast<float *>(gameBase + 0x9ac940);
  projection[0] = projection[5] = projection[10] = projection[11] = 1;
  projection[14] = -1;
  memcpy(view, projection, 64);
  modelView = view;
  skinBodies.clear();
  currentSkin.model = reinterpret_cast<float *>(root + 0x24c);
  identity(nativeMatrix, 10);
  identity(childMatrix, 12);
  pack(nativeMatrix, nativePacked);
  pack(childMatrix, childPacked);
  modelOwner = root;
  rememberSubmittedPalette(nativePacked, submittedPacked, 3);
  modelOwner = child;
  memcpy(child + 0x24c, childMatrix, 64);
  outputWidth = 3440;
  outputHeight = 1440;
  actualViewport.Width = 3440;
  actualViewport.Height = 1440;
  game<int>(0x508b14) = 1920;
  game<int>(0x508b18) = 1440;
  shaderDraw = verifyNativeDraw;
  interpolateMotion = true;
  for (unsigned mode : {8u, 15u, 30u, 31u, 33u}) {
    assert(shaderDrawHook(nullptr, nullptr,
                          reinterpret_cast<float *>(child + 0x24c), view, mode,
                          0, nullptr, nullptr) == 123);
    assert(!drawArgumentsFiltered && worldShaderDepth == 0);
    assert(game<int>(0x508b14) == 1920 && game<int>(0x508b18) == 1440);
  }
  // Real native root return address and recursive thiscall forwarding. All
  // named equipment shares this path; no goggles-specific name check exists.
  float lightView[16];identity(lightView,73.125f);
  casterRoot=root;casterChild=child;memcpy(casterNative,nativePacked,48);
  memcpy(casterSubmitted,submittedPacked,48);
  memcpy(unchangedCamera,cameraNative,64);memcpy(unchangedCamera+16,cameraRendered,64);
  unchangedCameraFrame=cameraFrame;
  modelDraw=verifyCasterModel;shaderDraw=verifyCasterDraw;
  auto cast=reinterpret_cast<ModelDrawFn>(callerThunk(0x369853,
      reinterpret_cast<void*>(modelDrawHook),6));
  expectCasterScope=true;expectCasterFiltering=true;
  auto checkCaster=[&] {
    assert(cast(root,reinterpret_cast<float*>(0x1234),lightView,0xabcd,0xffffffff,
        reinterpret_cast<void*>(0x5678),reinterpret_cast<void*>(0x9999))==0xfedcba00u);
    assert(!shadowCasterDepth && !drawArgumentsFiltered && worldShaderDepth==0 && modelOwner==child && modelView==view);
    assert(game<int>(0x508b14)==1920 && game<int>(0x508b18)==1440);
  };
  for(unsigned edge:{256u,512u,1024u,2048u}) {
    actualViewport.Width=actualViewport.Height=edge;checkCaster();
    // Shadow-first and scene-first pairing each use that pass's actual skin
    // submission, while sharing identical attachment local history.
    modelOwner=root;modelView=view;rememberSubmittedPalette(nativePacked,submittedPacked,3);
    modelOwner=child;shaderDraw=verifyNativeDraw;actualViewport.Width=3440;actualViewport.Height=1440;
    assert(shaderDrawHook(nullptr,nullptr,reinterpret_cast<float*>(child+0x24c),view,33,0,nullptr,nullptr)==123);
    memcpy(unchangedCamera,cameraNative,64);memcpy(unchangedCamera+16,cameraRendered,64);
    unchangedCameraFrame=cameraFrame;shaderDraw=verifyCasterDraw;
    actualViewport.Width=actualViewport.Height=edge;checkCaster();
  }
  expectCasterFiltering=false;
  actualViewport.Width=255;actualViewport.Height=255;checkCaster();
  actualViewport.Width=256;actualViewport.Height=512;checkCaster();
  actualViewport.Width=actualViewport.Height=256;
  game<int>(0x520880)=1;game<uintptr_t>(0x520468)=0x1234;checkCaster();
  game<int>(0x520880)=0;game<uintptr_t>(0x520468)=0;
  expectCasterScope=false;
  assert(modelDrawHook(root,nullptr,reinterpret_cast<float*>(0x1234),lightView,0xabcd,0xffffffff,
      reinterpret_cast<void*>(0x5678),reinterpret_cast<void*>(0x9999))==0xfedcba00u);
  assert(!shadowCasterDepth && modelOwner==child && modelView==view);
  modelOwner=root;rememberSubmittedPalette(nativePacked,submittedPacked,3);modelOwner=child;
  shaderDraw=verifyNativeDraw;
  actualViewport.Width=3440;actualViewport.Height=1440;
  particleDraw = verifyParticleDraw;
  expectedAxes = view;
  expectParticleScope = true;
  assert(particleDrawHook(root, nullptr, cameraNative, view, 0x123) == 456);
  assert(projectedGeometryDepth == 0 && game<int>(0x508b14) == 1920);
  expectParticleScope = false;
  hudDepth = 1;
  assert(particleDrawHook(root, nullptr, cameraNative, view, 0x123) == 456);
  hudDepth = 0;
  actualViewport.Width = actualViewport.Height = 256;
  assert(particleDrawHook(root, nullptr, cameraNative, view, 0x123) == 456);
  // Alternate sky projection recovers the same view, then shares main history.
  actualViewport.Width=3440; actualViewport.Height=1440;
  memcpy(reinterpret_cast<void*>(gameBase+0x9a7900),view,64);
  float *alternateProjection=reinterpret_cast<float*>(gameBase+0x975d18);
  memcpy(alternateProjection,projection,64);
  alternateProjection[0]=2; alternateProjection[5]=3;
  auto alternateVP=reinterpret_cast<float*>(gameBase+0x9a9c00);
  memcpy(alternateVP,alternateProjection,64);
  float cameraPacked[16], cameraResult[16];
  for(unsigned r=0;r<4;++r) for(unsigned c=0;c<4;++c)
    cameraPacked[r*4+c]=alternateVP[c*4+r];
  drawingView=alternateVP;
  assert(smoothCamera(cameraPacked,cameraResult));
  assert(cameraResult[0]==2 && cameraResult[5]==3);
  assert(cameraRendered[0]==1 && cameraNative[0]==1);
  alternateVP[12]=10;
  cameraPacked[3]=10;
  assert(!smoothCamera(cameraPacked,cameraResult)); // Different camera excluded.
  drawingView=nullptr;
  // x86 x87 sparse/generic projection arithmetic differs by a depth ULP.
  // A rotated stationary floor and its coplanar shadow now share exact world
  // bytes and one computed VP, including non-integer projection coefficients.
  float savedProjection[16],savedMain[16],staticWorld[16],filteredWorld[16],filteredVP[16];
  memcpy(savedProjection,projection,64);
  auto mainVP=reinterpret_cast<float*>(gameBase+0x9a7900);
  memcpy(savedMain,mainVP,64);
  float sx=std::sin(.123f),cx=std::cos(.123f),sy=std::sin(.217f),cy=std::cos(.217f),
        sz=std::sin(.713f),cz=std::cos(.713f);
  float stationary[16]={cy*cz,cy*sz,-sy,0,sx*sy*cz-cx*sz,sx*sy*sz+cx*cz,sx*cy,0,
      cx*sy*cz+sx*sz,cx*sy*sz-sx*cz,cx*cy,0,323.234f,578.13f,102.128f,1};
  memcpy(staticWorld,stationary,64);
  projection[0]=1.237f;projection[5]=2.175f;projection[10]=1.000064f;projection[14]=-10.000641f;
  multiplyMatrix(stationary,projection,mainVP);
  renderPoses.erase({projection,mainVP,1});
  auto savedOwner=modelOwner;auto savedDrawingModel=drawingModel;
  modelOwner=nullptr;drawingModel=nullptr;drawingView=mainVP;
  assert(filterDrawArguments(staticWorld,mainVP,filteredWorld,filteredVP));
  assert(!memcmp(filteredWorld,staticWorld,64));
  assert(!memcmp(filteredVP,cameraRendered,64));
  assert(!memcmp(staticWorld,stationary,64));
  // A 10-degree authored turn at a stationary eye far from the map origin
  // changes native view translation by >500, yet the eye must not teleport.
  for(bool reflected:{false,true}) for(float phase:{0.f,.25f,.5f,.75f,1.f})
      for(float offset:{0.f,10000.f}) {
    float before[16],after[16],beforeView[16],afterView[16];
    identity(before);identity(after);
    float angle=.174532925f;
    after[0]=after[5]=std::cos(angle);after[1]=std::sin(angle);after[4]=-after[1];
    if(reflected) {before[0]=-before[0];for(unsigned i=0;i<3;++i)after[i]=-after[i];}
    before[12]=after[12]=3000+offset;before[13]=after[13]=4000;before[14]=after[14]=200;
    assert(inverseAffine(before,beforeView) && inverseAffine(after,afterView));
    float dx=beforeView[12]-afterView[12],dy=beforeView[13]-afterView[13];
    assert(dx*dx+dy*dy>250000);
    multiplyMatrix(afterView,projection,mainVP);
    auto key=SkinKey{projection,mainVP,1};renderPoses.erase(key);
    auto &history=renderPoses[key];float previousPose[12],currentPose[12];
    pack(before,previousPose);pack(after,currentPose);
    history.previous.assign(previousPose,previousPose+12);history.current.assign(currentPose,currentPose+12);
    history.tick=game<unsigned>(0x1de47f4);history.lastSeen=frames;history.frame=~0u;
    sharedFrame=frames;sharedFrameTime=nowSeconds();history.stamp=sharedFrameTime-double(phase)/30;
    assert(filterDrawArguments(staticWorld,mainVP,filteredWorld,filteredVP));
    float recoveredView[16],recoveredEye[16];
    assert(factorPerspectiveView(filteredVP,projection,recoveredView) && inverseAffine(recoveredView,recoveredEye));
    assert(std::abs(recoveredEye[12]-(3000+offset))<.05f && std::abs(recoveredEye[13]-4000)<.05f);
    assert(std::abs(std::abs(recoveredEye[0])-std::cos(angle*phase))<.0001f);
    assert(!memcmp(filteredVP,cameraRendered,64) && history.rejected==0);
  }
  // True physical-eye teleports retain the existing native-pose fallback.
  for(float distance:{500.f,700.f}) {
    float before[16],after[16],afterView[16],prior[12],current[12];
    identity(before,3000);identity(after,3000+distance);
    assert(inverseAffine(after,afterView));multiplyMatrix(afterView,projection,mainVP);
    auto key=SkinKey{projection,mainVP,1};renderPoses.erase(key);auto &history=renderPoses[key];
    pack(before,prior);pack(after,current);
    history.previous.assign(prior,prior+12);history.current.assign(current,current+12);
    history.tick=game<unsigned>(0x1de47f4);history.lastSeen=frames;history.frame=~0u;
    sharedFrame=frames;sharedFrameTime=nowSeconds();history.stamp=sharedFrameTime-1./60;
    assert(filterDrawArguments(staticWorld,mainVP,filteredWorld,filteredVP));
    assert(history.rejected==1 && !memcmp(history.result.data(),current,48));
  }
  float invalidView[16]{};
  drawingView=invalidView;memset(cameraPacked,0,64);
  assert(!smoothCamera(cameraPacked,cameraResult));
  renderPoses.erase({projection,mainVP,1});
  sharedFrame=~0u;
  memcpy(projection,savedProjection,64);memcpy(mainVP,savedMain,64);
  modelOwner=savedOwner;drawingModel=savedDrawingModel;drawingView=nullptr;
  // Manual book is a separate native camera pass, independent of freecam.
  game<int>(0x520880)=1;game<uintptr_t>(0x520468)=0x1234;
  drawingView=view; cameraFrame=frames;
  assert(!smoothCamera(cameraPacked,cameraResult) && cameraFrame==~0u);
  assert(renderPoses.find({projection,reinterpret_cast<float*>(gameBase+0x9a7900),1})==renderPoses.end());
  game<int>(0x520880)=0;game<uintptr_t>(0x520468)=0;
  drawingView=nullptr;
  // Actual getter/projection/panel ABIs restore subpixel movement without
  // modifying gameplay matrices or the layout's fixed menu/edge clamps.
  bubbleAnchor=verifyBubbleAnchor; bubblePanel=verifyBubblePanel; projectVector=verifyProjection;
  auto anchorCall=reinterpret_cast<BubbleAnchorFn>(callerThunk(0x1cd589,
      reinterpret_cast<void*>(bubbleAnchorHook),1));
  using ProjectionCaller=float*(__stdcall*)(float*,const float*,const float*);
  auto projectionCall=reinterpret_cast<ProjectionCaller>(callerThunk(0x1cd5b4,
      reinterpret_cast<void*>(vectorHook),3,false));
  auto panelCall=reinterpret_cast<BubblePanelFn>(callerThunk(0x1cd858,
      reinterpret_cast<void*>(bubblePanelHook),6));
  float bubbleMatrix[16],anchor[4],projected[4];identity(bubbleMatrix,10);
  auto &popupPose=renderPoses[{nullptr,bubbleMatrix,1}];
  float popupPacked[12];pack(bubbleMatrix,popupPacked);
  popupPose.current.assign(popupPacked,popupPacked+12);popupPose.result=popupPose.current;
  popupPose.result[3]=7.25f;popupPose.frame=frames;
  bubbleScope=true;worldDepth=1;bubbleSavedX=2;bubbleSavedY=4;
  assert(anchorCall(bubbleMatrix,anchor)==anchor && anchor[0]==7.25f && bubbleMatrix[12]==10);
  cameraFrame=frames;memcpy(cameraNative,bubbleMatrix,64);
  assert(projectionCall(projected,anchor,bubbleMatrix)==projected);
  assert(bubbleProjected && std::abs(bubbleX-463.333333f)<1e-3 && bubbleY==210);
  bubbleY+=.625f;
  assert(panelCall(reinterpret_cast<void*>(0x1234),400,150,80,52,.2f,.7f)==777);
  assert(panelCall(reinterpret_cast<void*>(0x1234),0,32,80,52,.2f,.7f)==777);
  bubbleScope=false;worldDepth=0;spriteXDelta=spriteYDelta=0;
  // Restore the prior generic particle fixture camera.
  memcpy(cameraNative,view,64);
  // Local animation and its parent must share the same interpolation interval.
  // Native bone moves 10 -> 20, submitted bone is 15; local offset moves 2
  // -> 4. At half a tick the child is 15 + 3, not the stepped 15 + 4.
  ++frames;
  game<unsigned>(0x1de47f4) = 2;
  sharedFrame = frames;
  sharedTick = 2;
  sharedFrameTime = 100.;
  sharedTickStamp = 100. - 1. / 60.;
  identity(nativeMatrix, 20);
  identity(submittedMatrix, 15);
  identity(childMatrix, 24);
  pack(nativeMatrix, nativePacked);
  pack(submittedMatrix, submittedPacked);
  pack(childMatrix, childPacked);
  modelOwner = root;
  rememberSubmittedPalette(nativePacked, submittedPacked, 3);
  modelOwner = child;
  drawingModel = reinterpret_cast<float *>(child + 0x24c);
  assert(smoothAttachment(childPacked, result));
  assert(std::abs(result[3] - 18.f) < 1e-4);
  // Gameplay-owned matrices are untouched by the render transport.
  assert(childPacked[3] == 24 && nativePacked[3] == 20);
  // Exercise the actual five-callsite __thiscall/__fastcall bridge with exact
  // native return RVAs, including unrelated callers and scope suspension.
  nativePitch = verifyPitchRotation;
  orbitControls.pitch = .2f;
  cameraPitchScope = true;
  float rotationVector[4] = {42.f, 0, 0, 0};
  for (unsigned returnRva :
       {0x314164u, 0x3141e7u, 0x3145bbu, 0x3149d1u, 0x314b80u}) {
    auto call = reinterpret_cast<NativePitchFn>(
        callerThunk(returnRva, reinterpret_cast<void *>(cameraPitchHook), 1));
    call(rotationVector, .1f);
    assert(std::abs(capturedPitch - .3f) < 1e-6);
    call(rotationVector, .07f);
    assert(std::abs(capturedPitch - .27f) < 1e-6); // U + k*C, not k*(U+C).
  }
  auto unrelated = reinterpret_cast<NativePitchFn>(
      callerThunk(0x314220, reinterpret_cast<void *>(cameraPitchHook), 1));
  unrelated(rotationVector, .1f);
  assert(std::abs(capturedPitch - .1f) < 1e-6);
  cameraPitchScope = false;
  auto pitchCall = reinterpret_cast<NativePitchFn>(gameBase + 0x314164 - 9);
  pitchCall(rotationVector, .1f);
  assert(std::abs(capturedPitch - .1f) < 1e-6);

  // A missing HUD after D3D Reset suspends input, rather than erasing the
  // angle.
  auto blocker = reinterpret_cast<unsigned char *>(gameBase + 0x335670);
  DWORD previousProtection;
  assert(
      VirtualProtect(blocker, 6, PAGE_EXECUTE_READWRITE, &previousProtection));
  memcpy(blocker, "\x33\xc0\xc3", 3); // Native manual gate permits gameplay.
  FlushInstructionCache(GetCurrentProcess(), blocker, 6);
  nativeCamera = verifyCameraUpdate;
  supported = cameraHooksActive = modernSettings.freeCamera = true;
  modernSettings.cameraOutOfTownOnly=false;
  hudSeen = false;
  game<int>(0x1264a30)=1; // Ordinary native manual camera.
  resetCameraControls();
  assert(std::abs(orbitControls.pitch - .2f) < 1e-6);
  expectedCameraScope = true;
  assert(cameraUpdateHook(nativeMatrix, submittedMatrix) == 77);
  assert(std::abs(orbitControls.pitch - .2f) < 1e-6);
  memcpy(blocker, "\xb8\x01\x00\x00\x00\xc3", 6); // Script owns camera.
  game<int>(0x126441c)=1;
  FlushInstructionCache(GetCurrentProcess(), blocker, 6);
  expectedCameraScope = false;
  assert(cameraUpdateHook(nativeMatrix, submittedMatrix) == 77);
  assert(orbitControls.pitch == 0);
  // Idle outdoors follows the authored camera until a fresh right-stick input.
  game<int>(0x126441c)=0; game<int>(0x1264a30)=0;
  hudSeen=true; lastHudFrame=frames;
  expectedCameraScope=false; fakeAuthoredCamera=true;
  assert(cameraUpdateHook(nativeMatrix,submittedMatrix)==77);
  assert(!fixedOrbitEngaged && game<int>(0x1264a30)==0);
  assert(game<float>(0x1d7b5b0+4)==700 && orbitControls.pitch==0);
  WNDCLASSA cameraClass{};
  cameraClass.lpfnWndProc=DefWindowProcA;
  cameraClass.hInstance=GetModuleHandleA(nullptr);
  cameraClass.lpszClassName="GuruminCameraFixture";
  assert(RegisterClassA(&cameraClass));
  gameWindow=CreateWindowA(cameraClass.lpszClassName,"Camera fixture",
      WS_OVERLAPPEDWINDOW,0,0,120,100,nullptr,nullptr,cameraClass.hInstance,nullptr);
  assert(gameWindow); ShowWindow(gameWindow,SW_SHOW); SetForegroundWindow(gameWindow);
  assert(GetForegroundWindow()==gameWindow);
  game<int>(0x791ec0)=1; game<int>(0x1e280d4)=1;
  game<DWORD>(0x1e280e4)=0; game<NativeXInputFn>(0x1e280cc)=verifyXInput;
  nativeInput=verifyInput;
  int inputState[20]{};
  cameraInputHook(inputState);
  assert(cameraUpdateHook(nativeMatrix,submittedMatrix)==77);
  assert(!fixedOrbitEngaged);
  testRightX=20000; cameraInputHook(inputState);
  expectedCameraScope=false; expectedFixedCamera=true;
  assert(cameraUpdateHook(nativeMatrix,submittedMatrix)==77);
  assert(fixedOrbitEngaged && game<int>(0x1264a30)==0);
  assert(game<float>(0x1d7b5b0+4)==700 && orbitControls.pitch==0);
  // Moving anchors remain authoritative after user yaw, without feedback.
  game<int>(0x1264454)=17;
  ++game<unsigned>(0x1de47f4); cameraInputHook(inputState);
  assert(cameraUpdateHook(nativeMatrix,submittedMatrix)==77);
  assert(fixedOrbitYaw!=0 && game<int>(0x1264454)==17);
  float beforeShift[4]; memcpy(beforeShift,orbitRenderPose.eye.v,sizeof(beforeShift));
  float offsetYaw=fixedOrbitYaw;
  authoredShift=50; ++game<int>(0x126469c); // Authored anchor index transition.
  assert(cameraUpdateHook(nativeMatrix,submittedMatrix)==77);
  assert(fixedOrbitYaw==offsetYaw && std::abs(orbitRenderPose.eye.v[0]-beforeShift[0]-50)<1e-4);
  assert(std::abs(orbitRenderPose.eye.v[1]-beforeShift[1])<1e-4);
  for(unsigned rva:{0x1d7b5b0u,0x1cf17a0u,0x1dffed8u}) {
    assert(game<float>(rva)==80 && game<float>(rva+4)==700 && game<float>(rva+8)==600);
  }
  assert(submittedMatrix[0]==80 && submittedMatrix[1]==20 && submittedMatrix[2]==100);
  // Exact aggregate cdecl ABI; caller's stack vectors and carry stay native.
  assert(orbitRenderPose.prepared && nativeMatrix[0]==80 && nativeMatrix[1]==700);
  nativeCameraView=verifyCameraView; nativeCameraWorld=verifyCameraWorld;
  using ViewCaller=float*(__stdcall*)(float*,CameraVector,CameraVector,float*,float);
  auto viewCall=reinterpret_cast<ViewCaller>(callerThunk(0x1a992a,
      reinterpret_cast<void*>(cameraViewHook),11,false));
  float builtView[16]; expectedAxes=view;
  CameraVector rawEye=orbitRenderPose.nativeEye,rawTarget=orbitRenderPose.nativeTarget;
  assert(viewCall(builtView,rawEye,rawTarget,view,.125f)==builtView);
  assert(orbitRenderPose.built && !memcmp(capturedViewEye.v,orbitRenderPose.eye.v,16));
  assert(!memcmp(rawEye.v,nativeMatrix,16));
  memcpy(reinterpret_cast<void*>(gameBase+0x1dffed8),rawEye.v,16);
  memcpy(reinterpret_cast<void*>(gameBase+0x1e00ee8),rawTarget.v,16);
  memcpy(reinterpret_cast<void*>(gameBase+0x9a9b00),builtView,64);
  // Actual cdecl movement adapter with adjacent scalar outputs. Authored
  // transitions retain native carry, while gameplay follows the visible basis.
  nativeCameraMovement=verifyCameraMovement;
  using MovementCaller=unsigned(__stdcall*)(float*,float*,float,float);
  auto movementCall=reinterpret_cast<MovementCaller>(callerThunk(0x21d499,
      reinterpret_cast<void*>(cameraMovementHook),4,false));
  auto unrelatedMovement=reinterpret_cast<MovementCaller>(callerThunk(0x21d550,
      reinterpret_cast<void*>(cameraMovementHook),4,false));
  OrbitRenderPose savedMovementPose=orbitRenderPose;
  char savedMap[64];memcpy(savedMap,reinterpret_cast<void*>(gameBase+0x527110),64);
  char savedOrbitMap[64];memcpy(savedOrbitMap,orbitMapName,64);
  strcpy(reinterpret_cast<char*>(gameBase+0x527110),"mp_0C0");
  strcpy(orbitMapName,"mp_0C0");
  for(float yaw:{0.f,1.570796327f,3.141592654f,-1.570796327f}) {
    orbitRenderPose=savedMovementPose;
    float e[]={80,20-100.f,300,1},t[]={80,20,100,1};
    memcpy(orbitRenderPose.eye.v,e,16);memcpy(orbitRenderPose.target.v,t,16);
    assert(gurumin::orbitFromAnchor(orbitRenderPose.eye.v,orbitRenderPose.target.v,yaw,.2f));
    for(unsigned age:{0u,1u}) {
      orbitRenderPose.tick=game<unsigned>(0x1de47f4)-age;
      float adjacent[2];unsigned beforeCalls=movementCalls;
      for(gurumin::Stick stick:{gurumin::Stick{0,1},gurumin::Stick{1,0},gurumin::Stick{.6f,.8f}}) {
        assert(movementCall(adjacent,adjacent+1,stick.x,stick.y)==0);
        assert(std::abs(adjacent[0]-(stick.x*std::cos(yaw)-stick.y*std::sin(yaw)))<1e-5);
        assert(std::abs(adjacent[1]-(stick.x*std::sin(yaw)+stick.y*std::cos(yaw)))<1e-5);
        assert(game<float>(0x1d7b640)==stick.x && game<float>(0x1d7b644)==stick.y);
        assert(!memcmp(reinterpret_cast<void*>(gameBase+0x1d7b5f0),
            reinterpret_cast<void*>(gameBase+0x1d7b5b0),16));
        assert(!memcmp(reinterpret_cast<void*>(gameBase+0x1d7b600),
            reinterpret_cast<void*>(gameBase+0x1ae4900),16));
      }
      assert(movementCalls==beforeCalls+3);
    }
  }
  float adjacent[2];
  auto nativeFallback=[&]() {
    unsigned before=movementCalls;
    assert(movementCall(adjacent,adjacent+1,0,1)==0);
    assert(movementCalls==before+1 && adjacent[0]==.125f && adjacent[1]==-.25f);
  };
  auto mappedForward=[&]() {
    assert(movementCall(adjacent,adjacent+1,0,1)==0);
    assert(std::abs(adjacent[0]-1)<1e-5 && std::abs(adjacent[1])<1e-5);
  };
  // Established mapping survives input suspension without a second smoother.
  cameraSample.valid=false;hudSeen=false; mappedForward();hudSeen=true;
  assert(unrelatedMovement(adjacent,adjacent+1,0,1)==0);
  assert(adjacent[0]==.125f && adjacent[1]==-.25f);
  assert(movementCall(adjacent,adjacent+1,0,0)==0 && std::signbit(adjacent[0]));
  orbitRenderPose.tick=game<unsigned>(0x1de47f4)-2;nativeFallback();
  orbitRenderPose.tick=game<unsigned>(0x1de47f4);
  unsigned movementTick=game<unsigned>(0x1de47f4);
  game<unsigned>(0x1de47f4)=0;orbitRenderPose.tick=~0u;mappedForward();
  game<unsigned>(0x1de47f4)=movementTick;orbitRenderPose.tick=movementTick;
  game<int>(0x515ec4)=1;nativeFallback();game<int>(0x515ec4)=0;
  game<int>(0x126441c)=1;nativeFallback();game<int>(0x126441c)=0;
  game<int>(0x1264a30)=1;nativeFallback();game<int>(0x1264a30)=0;
  game<int>(0x791ec0)=0;nativeFallback();game<int>(0x791ec0)=1;
  modernSettings.freeCamera=false;nativeFallback();modernSettings.freeCamera=true;
  orbitRenderPose.built=false;nativeFallback();orbitRenderPose.built=true;
  ++orbitRenderPose.scene;nativeFallback();--orbitRenderPose.scene;
  strcpy(reinterpret_cast<char*>(gameBase+0x527110),"mp_020");nativeFallback();
  strcpy(reinterpret_cast<char*>(gameBase+0x527110),"mp_0C0");
  game<float>(0x1cf17a0)+=1;nativeFallback();game<float>(0x1cf17a0)-=1;
  game<float>(0x9a9b00)+=1;nativeFallback();game<float>(0x9a9b00)-=1;
  assert(movementCall(adjacent,adjacent+1,NAN,1)==0 && adjacent[0]==.125f);
  orbitRenderPose.eye.v[0]=NAN;nativeFallback();
  assert(cameraMovementOutputsIndependent(adjacent,adjacent+1));
  assert(!cameraMovementOutputsIndependent(adjacent,adjacent));
  assert(!cameraMovementOutputsIndependent(reinterpret_cast<float*>(gameBase+0x1d7b640),adjacent));
  memcpy(reinterpret_cast<void*>(gameBase+0x527110),savedMap,64);
  memcpy(orbitMapName,savedOrbitMap,64);orbitRenderPose=savedMovementPose;
  expectWorldPublication=true;
  cameraWorldHook(); ++frames; lastHudFrame=frames; cameraWorldHook();
  assert(worldCalls==2 && !memcmp(reinterpret_cast<void*>(gameBase+0x1cf17a0),rawEye.v,16));
  assert(!memcmp(reinterpret_cast<void*>(gameBase+0x1dffed8),rawEye.v,16));
  expectWorldPublication=false; game<float>(0x9a9b00)+=1;
  cameraWorldHook(); assert(worldCalls==3);
  memcpy(reinterpret_cast<void*>(gameBase+0x9a9b00),builtView,64);
  expectWorldPublication=true; replaceWorldEye=true; cameraWorldHook();
  assert(game<float>(0x1cf17a0)==1234 && !orbitRenderPose.prepared);
  replaceWorldEye=expectWorldPublication=false;
  assert(!cameraOutputsIndependent(reinterpret_cast<float*>(gameBase+0x1d7b5b0),submittedMatrix));
  assert(!cameraOutputsIndependent(nativeMatrix,nativeMatrix+1));
  // Neutral/menu/disconnect retain ownership, but integration rebases.
  testRightX=0; cameraInputHook(inputState);
  assert(cameraUpdateHook(nativeMatrix,submittedMatrix)==77 && fixedOrbitEngaged);
  game<int>(0x515ec4)=1;
  assert(cameraUpdateHook(nativeMatrix,submittedMatrix)==77 && fixedOrbitEngaged);
  game<int>(0x515ec4)=0; game<int>(0x1e280d4)=0;
  assert(cameraUpdateHook(nativeMatrix,submittedMatrix)==77 && fixedOrbitEngaged);
  // A fresh old-scene sample cannot force the first frame of the next area.
  game<int>(0x1e280d4)=1; testRightX=20000; cameraInputHook(inputState);
  ++game<int>(0xf32c78);
  fakeAuthoredCamera=true; expectedCameraScope=expectedFixedCamera=false;
  assert(cameraUpdateHook(nativeMatrix,submittedMatrix)==77);
  assert(!fixedOrbitEngaged && !cameraSample.valid && orbitControls.pitch==0);
  // Extra outdoor contraction occurs only while actively lowering, including
  // repeated calls at the same tick. Neutral/up/yaw leave the authored radius.
  std::vector<char> cameraActor(0x2500);
  expectedGeometry=reinterpret_cast<void*>(0x9876);
  *reinterpret_cast<void**>(cameraActor.data()+0x231c)=expectedGeometry;
  game<char*>(0x1264684)=cameraActor.data(); cameraSegment=verifySegment;
  fakeAuthoredCamera=true; expectedCameraScope=false; expectedFixedCamera=true;
  fixedOrbitEngaged=true; orbitControls.pitch=-.2f;
  modernSettings.invertY=false;
  testRightX=0; testRightY=20000; cameraInputHook(inputState);
  assert(cameraUpdateHook(nativeMatrix,submittedMatrix)==77 && segmentCalls==1);
  float contractedRadius=std::hypot(std::hypot(orbitRenderPose.eye.v[0]-submittedMatrix[0],
      orbitRenderPose.eye.v[1]-submittedMatrix[1]),orbitRenderPose.eye.v[2]-submittedMatrix[2]);
  assert(contractedRadius<400 && nativeMatrix[3]==1 && submittedMatrix[3]==1);
  assert(cameraUpdateHook(nativeMatrix,submittedMatrix)==77 && segmentCalls==2);
  testRightY=0; cameraInputHook(inputState);
  assert(cameraUpdateHook(nativeMatrix,submittedMatrix)==77 && segmentCalls==2);
  testRightY=-20000; cameraInputHook(inputState);
  assert(cameraUpdateHook(nativeMatrix,submittedMatrix)==77 && segmentCalls==2);
  testRightY=0; testRightX=20000; cameraInputHook(inputState);
  assert(cameraUpdateHook(nativeMatrix,submittedMatrix)==77 && segmentCalls==2);
  modernSettings.invertY=true; testRightX=0; testRightY=-20000;
  cameraInputHook(inputState);
  assert(cameraUpdateHook(nativeMatrix,submittedMatrix)==77 && segmentCalls==3);
  // Output aliases are never transformed or passed to the extra query.
  assert(cameraUpdateHook(reinterpret_cast<float*>(gameBase+0x1d7b5b0),submittedMatrix)==77);
  assert(segmentCalls==3 && game<float>(0x1d7b5b0)==80);
  expectedFixedCamera=false; fakeAuthoredCamera=false;
  game<int>(0x1264a34)=1; expectedCameraScope=false;
  assert(cameraUpdateHook(nativeMatrix,submittedMatrix)==77);
  assert(segmentCalls==3 && !fixedOrbitEngaged && orbitControls.pitch==0 && fixedOrbitYaw==0);
  game<int>(0x1264a34)=0;
  fixedOrbitEngaged=true;fixedOrbitYaw=.4f;orbitControls.pitch=.2f;
  game<int>(0x520880)=1;game<uintptr_t>(0x520468)=0x1234;
  assert(cameraUpdateHook(nativeMatrix,submittedMatrix)==77);
  assert(fixedOrbitEngaged && std::abs(fixedOrbitYaw-.4f)<1e-6 && std::abs(orbitControls.pitch-.2f)<1e-6);
  game<int>(0x520880)=0;game<uintptr_t>(0x520468)=0;
  game<int>(0x12646ac)=5;
  assert(cameraUpdateHook(nativeMatrix,submittedMatrix)==77 && segmentCalls==3);
  game<int>(0x12646ac)=0; game<int>(0x1264a30)=1;
  memcpy(blocker,"\x33\xc0\xc3",3);
  FlushInstructionCache(GetCurrentProcess(),blocker,6);
  expectedCameraScope=true;
  orbitCameraKind=1; fixedOrbitEngaged=true; fixedOrbitYaw=.8f; orbitControls.pitch=.2f;
  assert(cameraUpdateHook(nativeMatrix,submittedMatrix)==77);
  assert(!fixedOrbitEngaged && fixedOrbitYaw==0 && orbitControls.pitch==0); // Ownership-kind transition.
  fakeCameraOutputs=false; cameraSegment=nullptr;
  expectedCameraScope=false;
  // Interiors-only gates ownership, not merely integration.
  memcpy(blocker,"\x33\xc0\xc3",3);
  FlushInstructionCache(GetCurrentProcess(),blocker,6);
  game<int>(0x12646ac)=0; game<int>(0x1264a30)=1;
  modernSettings.cameraOutOfTownOnly=true;
  game<int>(0xf32c78)=2;
  strcpy(reinterpret_cast<char*>(gameBase+0x527110),"mp_020");
  assert(orbitCameraOwned());
  game<int>(0xf32c78)=12;
  strcpy(reinterpret_cast<char*>(gameBase+0x527110),"mp_0C0");
  orbitScene=12; fixedOrbitEngaged=true; orbitControls.pitch=.2f;
  cameraSample.valid=true;
  assert(cameraUpdateHook(nativeMatrix,submittedMatrix)==77);
  assert(!fixedOrbitEngaged && !cameraSample.valid && orbitControls.pitch==0);
  game<int>(0xf32c78)=102;
  strcpy(reinterpret_cast<char*>(gameBase+0x527110),"mp_102");
  assert(orbitCameraOwned());
  modernSettings.freeCamera=false;
  game<int>(0xf32c78)=2;
  strcpy(reinterpret_cast<char*>(gameBase+0x527110),"mp_020");
  assert(!orbitCameraOwned());
  modernSettings.freeCamera=true; modernSettings.cameraOutOfTownOnly=false;
  DestroyWindow(gameWindow); gameWindow=nullptr;

  // Exercise the actual draw adapter with a bounded D3D vtable fixture.
  // Native title pattern expands UVs rather than stretching tile density;
  // the iris keeps its UV slope and full-screen fade retains color/alpha.
  void *table[119]{};
  table[90]=reinterpret_cast<void*>(fakeFVF);
  table[48]=reinterpret_cast<void*>(fakeViewport);
  void **deviceTable=table;
  auto fakeDevice=reinterpret_cast<IDirect3DDevice9*>(&deviceTable);
  drawUP=captureQuad;
  modernSettings.antiAliasing=0;
  actualViewport={0,0,3440,1440,0,1};
  float quad[32]{};
  auto setQuad=[&](float left,float right,float u0,float u1) {
    for(int i=0;i<4;++i) {
      float *v=quad+i*8;
      v[0]=(i%2)?right:left; v[1]=(i/2)?1440:0;
      v[2]=.7f; v[3]=1;
      DWORD color=0x7fe0a000; memcpy(v+4,&color,4);
      v[6]=(i%2)?u1:u0; v[7]=(i/2)?3.75f:0;
    }
  };
  setQuad(0,1920,0,5);
  activeSpriteCaller=0x22d463;
  drawHook(fakeDevice,D3DPT_TRIANGLESTRIP,2,quad,32);
  assert(capturedQuad[0]==0 && capturedQuad[8]==3440);
  assert(std::abs((capturedQuad[14]-capturedQuad[6])/3440-5.f/1920)<1e-6);
  assert(std::abs((capturedQuad[14]+capturedQuad[6])/2-2.5f)<1e-6);
  setQuad(-660,2580,-.25f,1.25f);
  activeSpriteCaller=0x1df19b;
  drawHook(fakeDevice,D3DPT_TRIANGLESTRIP,2,quad,32);
  assert(capturedQuad[0]<=0 && capturedQuad[8]>=3440);
  assert(std::abs((capturedQuad[14]-capturedQuad[6])/
    (capturedQuad[8]-capturedQuad[0])-1.5f/3240)<1e-6);
  setQuad(0,1920,0,0);
  activeSpriteCaller=0; fullscreenSolidDepth=1;
  drawHook(fakeDevice,D3DPT_TRIANGLESTRIP,2,quad,32);
  assert(capturedQuad[0]==0 && capturedQuad[8]==3440);
  assert(!memcmp(capturedQuad+4,quad+4,8));
  fullscreenSolidDepth=0;
  drawHook(fakeDevice,D3DPT_TRIANGLESTRIP,2,quad,32);
  assert(capturedQuad[0]==760 && capturedQuad[8]==2680);
  // Independently specified native cdecl call sites, including each cinematic
  // bar path and script-owned full fades. Only X changes, not Y/color/depth.
  rectangleDevice=fakeDevice;solidRect=verifySolidRect;
  game<int>(0x508b14)=1920;game<int>(0x508b18)=1440;
  using SolidCaller=unsigned(__stdcall*)(int,int,int,int,unsigned,float,float);
  for(unsigned caller:{0x1d2e71u,0x1d2e9bu,0x1db546u,0x1db570u,0x32a391u,0x32a3beu}) {
    auto rect=reinterpret_cast<SolidCaller>(callerThunk(caller,
        reinterpret_cast<void*>(solidRectHook),7,false));
    for(int y:{0,446}) {
      assert(rect(0,y,640,y?480:34,0,.7f,.5f)==0xabcd5678u);
      assert(capturedQuad[0]==0 && capturedQuad[8]==3440);
      assert(capturedQuad[1]==y*3 && capturedQuad[17]==(y?480:34)*3);
      assert(*reinterpret_cast<unsigned*>(capturedQuad+2)==0x3f333333u && fullscreenSolidDepth==0);
    }
    rect(0,100,640,200,0,.7f,.5f);
    assert(capturedQuad[0]==760 && capturedQuad[8]==2680);
  }
  for(unsigned caller:{0x1df06au,0x32a33eu,0x32a418u,0x1d38b8u}) {
    auto rect=reinterpret_cast<SolidCaller>(callerThunk(caller,
        reinterpret_cast<void*>(solidRectHook),7,false));
    rect(0,0,640,480,0x123456,.7f,.5f);
    bool full=caller!=0x1d38b8u;
    assert(capturedQuad[0]==(full?0:760) && capturedQuad[8]==(full?3440:2680));
    assert(*reinterpret_cast<unsigned*>(capturedQuad+4)==0x123456);
  }
  drawUP=nullptr;
  // All four independently scoped sampling policies use the named asset
  // color role even with interpolation disabled; masks and RTs are excluded.
  void *textureTable[22]{};
  textureTable[2]=reinterpret_cast<void*>(fakeRelease);
  textureTable[10]=reinterpret_cast<void*>(fakeTextureType);
  textureTable[17]=reinterpret_cast<void*>(fakeTextureDesc);
  void **textureVtable=textureTable;
  fakeAsset=reinterpret_cast<IDirect3DBaseTexture9*>(&textureVtable);
  table[64]=reinterpret_cast<void*>(fakeGetTexture);
  game<uintptr_t>(0x1de47ec)=gameBase+0x198f6a0;
  strcpy(reinterpret_cast<char*>(gameBase+0x128c7d0),"test-color");
  char descriptor[0x50]{};
  int handles[]={0};
  *reinterpret_cast<int*>(descriptor+0x44)=1;
  *reinterpret_cast<int**>(descriptor+0x14)=handles;
  materialDescriptor=descriptor;
  interpolateMotion=false;
  for(bool uiFilter : {false,true}) for(bool worldFilter : {false,true}) {
    modernSettings.uiFiltering=uiFilter; modernSettings.worldFiltering=worldFilter;
    activeSpriteCaller=0x1cd111; worldMaterialDepth=0;
    assert(nearestAssetSampler(fakeDevice)==!uiFilter);
    activeSpriteCaller=0; worldMaterialDepth=1;
    assert(nearestAssetSampler(fakeDevice)==!worldFilter);
  }
  modernSettings.uiFiltering=modernSettings.worldFiltering=false;
  handles[0]=1; assert(!nearestAssetSampler(fakeDevice));
  handles[0]=2;
  *reinterpret_cast<int**>(descriptor+0x18)=reinterpret_cast<int*>(0xdeadbeef);
  assert(!nearestAssetSampler(fakeDevice));
  *reinterpret_cast<int**>(descriptor+0x18)=nullptr;
  handles[0]=0;
  *reinterpret_cast<int**>(descriptor+0x18)=reinterpret_cast<int*>(0xdeadbeef);
  assert(nearestAssetSampler(fakeDevice)); // Primary succeeds independently.
  *reinterpret_cast<int**>(descriptor+0x14)=reinterpret_cast<int*>(0xdeadbeef);
  assert(!nearestAssetSampler(fakeDevice));
  *reinterpret_cast<int**>(descriptor+0x14)=handles;
  *reinterpret_cast<int**>(descriptor+0x18)=nullptr;
  handles[0]=0; fakeAssetUsage=D3DUSAGE_RENDERTARGET;
  assert(!nearestAssetSampler(fakeDevice));
  fakeAssetUsage=0; strcpy(reinterpret_cast<char*>(gameBase+0x128c7d0),"shadow0");
  assert(!nearestAssetSampler(fakeDevice));
  worldMaterialDepth=0; activeSpriteCaller=0x1df19b;
  assert(!nearestAssetSampler(fakeDevice));
  activeSpriteCaller=0x1f3828; assert(!nearestAssetSampler(fakeDevice));
  activeSpriteCaller=0; materialDescriptor=nullptr;

  // Reward preview/billboard quad is thiscall, not stdcall: keep its texture
  // receiver in ECX while forwarding all six stack DWORDs and restoring size.
  projectedQuad=verifyProjectedQuad;
  auto quadCall=reinterpret_cast<ProjectedQuadFn>(callerThunk(0x323cfdu,
      reinterpret_cast<void*>(projectedQuadHook),6));
  game<int>(0x508b14)=1920;game<int>(0x508b18)=1440;
  assert(quadCall(reinterpret_cast<void*>(0x1234),64,1440,3440,0xabcdef,.7f,1)==789);
  assert(projectedGeometryDepth==0 && game<int>(0x508b14)==1920 && game<int>(0x508b18)==1440);
  quadNested=true;
  assert(quadCall(reinterpret_cast<void*>(0x1234),64,1440,3440,0xabcdef,.7f,1)==789);
  float vertexData[16]{};quadArguments[3]=uintptr_t(vertexData);
  assert(quadCall(reinterpret_cast<void*>(0x1234),64,1440,3440,uintptr_t(vertexData),.7f,1)==789);
  quadArguments[0]=0x80001001u;quadArguments[1]=0xf1234567u;
  quadArguments[2]=0xffffffffu;quadArguments[3]=0x80000000u;quadMode=0;
  assert(quadCall(reinterpret_cast<void*>(0x1234),quadArguments[0],quadArguments[1],quadArguments[2],quadArguments[3],.7f,0)==789);
  actualViewport.Width=actualViewport.Height=0;quadExpectedWidth=1920;
  assert(quadCall(reinterpret_cast<void*>(0x1234),quadArguments[0],quadArguments[1],quadArguments[2],quadArguments[3],.7f,0)==789);
  actualViewport.Width=3440;actualViewport.Height=1440;
  // Independently specified DWORD callers exercise full EAX (including AL=0)
  // and the other longest receiver bridges, with exact native stack counts.
  shadowBind=verifyBinder;modernSettings.shadowResolution=256;shadowFallback=false;
  using BinderCaller=DWORD(__thiscall*)(void*,int,int,int);
  auto binderCall=reinterpret_cast<BinderCaller>(callerThunk(0x3a4d00,
      reinterpret_cast<void*>(shadowBindHook),3));
  for(unsigned value:{0u,1u,0x87654321u,0xffff0000u}) {
    binderResult=value;assert(binderCall(reinterpret_cast<void*>(0x1234),731,492,7)==value);
  }
  shadowBind=nullptr;assert(binderCall(reinterpret_cast<void*>(0x1234),731,492,7)==0);
  sprite=verifySprite;
  using SpriteCaller=DWORD(__thiscall*)(void*,int,int,int,int,float,float,float,float,float,float,float,float,int);
  auto spriteCall=reinterpret_cast<SpriteCaller>(callerThunk(0x200100,
      reinterpret_cast<void*>(spriteHook),13));
  assert(spriteCall(reinterpret_cast<void*>(0x1234),1,2,3,4,.125f,.25f,.375f,.5f,.625f,.75f,.875f,1,9)==0xab00ff00u);
  shadowReceiver=verifyReceiver;expectedAxes=view;
  using ReceiverCaller=DWORD(__thiscall*)(void*,uintptr_t,const float*,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t);
  auto receiverCall=reinterpret_cast<ReceiverCaller>(callerThunk(0x200200,
      reinterpret_cast<void*>(shadowReceiverHook),14));
  assert(receiverCall(reinterpret_cast<void*>(0x1234),1,view,3,4,5,6,7,8,9,10,11,12,13,14)==0xab00ff00u);
  // Native acceptance commits atomically, preserves unknown preferences, and
  // Cancel/unrelated callers cannot persist a staged edit.
  char tempFolder[MAX_PATH], iniFile[MAX_PATH];
  assert(GetTempPathA(MAX_PATH, tempFolder));
  assert(GetTempFileNameA(tempFolder, "gmt", 0, iniFile));
  strcpy(modernIni, iniFile);
  assert(WritePrivateProfileStringA("GuruminModern", "ExistingPreference",
                                    "keep", iniFile));
  // The old option's values cannot be reinterpreted as the new town policy.
  for(const char *legacy:{"0","1"}) {
    assert(WritePrivateProfileStringA("GuruminModern","CameraInteriorsOnly",legacy,iniFile));
    loadModernSettings();
    assert(modernSettings.cameraOutOfTownOnly);
  }
  loadModernSettings();
  assert(!modernSettings.freeCamera && modernSettings.cameraOutOfTownOnly && modernSettings.invertX);
  assert(modernSettings.resolution.width==GetSystemMetrics(SM_CXSCREEN) &&
         modernSettings.resolution.height==GetSystemMetrics(SM_CYSCREEN));
  launcherWindow = reinterpret_cast<HWND>(0x1234);
  originalEndDialog = verifyDialogEnd;
  launcherDraft.resolution = {3440, 1440};
  launcherDraft.frameCap = 175;
  launcherDraft.freeCamera = true;
  launcherDraft.cameraOutOfTownOnly = true;
  launcherDraft.invertX = true;
  launcherDraft.shadowResolution=1024;
  launcherDraft.antiAliasing=2;
  launcherDraft.uiFiltering=false;
  launcherDraft.worldFiltering=true;
  pendingAcceptance = true;
  acceptedSettings = false;
  auto accept = reinterpret_cast<EndDialogFn>(
      callerThunk(0x17ed, reinterpret_cast<void *>(launcherEndDialog), 2));
  assert(accept(launcherWindow, IDOK) == FALSE);
  assert(acceptedSettings && !pendingAcceptance &&
         modernSettings.frameCap == 175);
  assert(GetPrivateProfileIntA("GuruminModern", "Width", 0, iniFile) == 3440);
  assert(GetPrivateProfileIntA("GuruminModern", "InvertCameraX", 0, iniFile) == 1);
  assert(GetPrivateProfileIntA("GuruminModern", "CameraOutOfTownOnly", 0, iniFile) == 1);
  loadModernSettings();
  assert(modernSettings.freeCamera && modernSettings.cameraOutOfTownOnly);
  assert(GetPrivateProfileIntA("GuruminModern","ShadowResolution",0,iniFile)==1024);
  assert(GetPrivateProfileIntA("GuruminModern","AntiAliasing",0,iniFile)==2);
  assert(GetPrivateProfileIntA("GuruminModern","UIFiltering",1,iniFile)==0);
  assert(GetPrivateProfileIntA("GuruminModern","WorldFiltering",0,iniFile)==1);
  char preference[16];
  GetPrivateProfileStringA("GuruminModern", "ExistingPreference", "",
                           preference, sizeof(preference), iniFile);
  assert(!strcmp(preference, "keep"));
  pendingAcceptance = true;
  acceptedSettings = false;
  launcherDraft.frameCap = 60;
  accept(launcherWindow, IDCANCEL);
  assert(!acceptedSettings &&
         GetPrivateProfileIntA("GuruminModern", "FrameCap", 0, iniFile) == 175);
  auto differentCaller = reinterpret_cast<EndDialogFn>(
      callerThunk(0x1800, reinterpret_cast<void *>(launcherEndDialog), 2));
  differentCaller(launcherWindow, IDOK);
  assert(!acceptedSettings && modernSettings.frameCap == 175);
  for(const char *key:{"FreeCamera","CameraOutOfTownOnly","InvertCameraX"})
    assert(WritePrivateProfileStringA("GuruminModern",key,"0",iniFile));
  loadModernSettings();
  assert(!modernSettings.freeCamera && !modernSettings.cameraOutOfTownOnly && !modernSettings.invertX);
  // Numeric cap preserves native synchronization; only explicit uncapped
  // mode requests immediate presentation. Exercise both CreateDevice and Reset.
  createDevice=verifyDeviceCreation; resetDevice=verifyDeviceReset;
  modernSettings.resolution={0,0}; supported=true;
  for(int cap:{0,175,-1}) {
    modernSettings.frameCap=cap;
    D3DPRESENT_PARAMETERS params{};
    params.Windowed=TRUE; params.BackBufferWidth=640;params.BackBufferHeight=480;
    params.PresentationInterval=D3DPRESENT_INTERVAL_ONE;
    expectedPresentation=cap<0?D3DPRESENT_INTERVAL_IMMEDIATE:D3DPRESENT_INTERVAL_ONE;
    IDirect3DDevice9 *device=nullptr;
    assert(deviceHook(nullptr,0,D3DDEVTYPE_HAL,nullptr,0,&params,&device)==D3DERR_INVALIDCALL);
    params.PresentationInterval=D3DPRESENT_INTERVAL_ONE;
    assert(resetHook(nullptr,&params)==D3DERR_INVALIDCALL);
  }
  modernSettings.frameCap=175;
  D3DPRESENT_PARAMETERS off{}; off.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
  expectedPresentation=D3DPRESENT_INTERVAL_IMMEDIATE;
  assert(resetHook(nullptr,&off)==D3DERR_INVALIDCALL);
  DeleteFileA(iniFile);
  puts(
      "Native rendering, camera ABI/reset and accepted settings checks passed");
}
