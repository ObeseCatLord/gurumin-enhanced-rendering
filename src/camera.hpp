// Native input and camera adapter; all offsets are RVAs for the verified build.
#include <xinput.h>
using NativeInputFn = void(__cdecl *)(int *);
using NativeCameraFn = unsigned(__cdecl *)(float *, float *);
using NativePitchFn = void(__thiscall *)(float *, float);
using ManualCameraBlockerFn = int(__cdecl *)();
using NativeXInputFn = DWORD(WINAPI *)(DWORD, XINPUT_STATE *);
static NativeInputFn nativeInput;
static NativeCameraFn nativeCamera;
static NativePitchFn nativePitch;
static bool cameraHooksActive, cameraPitchScope, establishedOrbit;
// Ownership outlives integration priming and temporary input suspension.
static bool fixedOrbitEngaged;
static float fixedOrbitYaw;
static int orbitCameraKind=-1;
static char orbitMapName[64];
struct CameraVector { float v[4]; };
static_assert(sizeof(CameraVector)==16,"Native by-value camera vector");
using CameraViewFn=float*(__cdecl*)(float*,CameraVector,CameraVector,float*,float);
using CameraWorldFn=void(__cdecl*)();
static CameraViewFn nativeCameraView;
static CameraWorldFn nativeCameraWorld;
using CameraMovementFn=unsigned(__cdecl*)(float*,float*,float,float);
static CameraMovementFn nativeCameraMovement;
struct OrbitRenderPose {
  CameraVector nativeEye{}, nativeTarget{}, eye{}, target{};
  float view[16]{};
  unsigned tick=~0u;
  int scene=0;
  bool prepared=false, built=false;
};
static OrbitRenderPose orbitRenderPose;
static int orbitScene;
static gurumin::OrbitControls orbitControls;
struct CameraSample {
  gurumin::Stick stick;
  double time = 0;
  DWORD device = ~0u;
  NativeXInputFn reader = nullptr;
  int scene = 0;
  bool valid = false;
};
static CameraSample cameraSample;
static bool fixedGameplayOrbit() {
  return game<int>(0x126441c) == 0 && game<int>(0x12646ac) == 0 &&
         game<int>(0x1264688) == 0 && game<int>(0x1264a30) == 0 &&
         game<int>(0x1264fd0) == 0;
}
static bool orbitCameraOwned() {
  if (!supported || !cameraHooksActive || !modernSettings.freeCamera)
    return false;
  if (diagnostics) {
    static unsigned nextTrace;
    if (frames >= nextTrace) {
      nextTrace = frames + 600;
      log("Camera ownership frame=%u script=%d mode=%d special=%d manual=%d "
          "block=%d control=%d scene=%d map=%.32s", frames, game<int>(0x126441c),
          game<int>(0x12646ac), game<int>(0x1264688), game<int>(0x1264a30),
          game<int>(0x1264fd0), game<int>(0x791ec0),game<int>(0xf32c78),
          reinterpret_cast<const char*>(gameBase+0x527110));
    }
  }
  if (modernSettings.cameraOutOfTownOnly &&
      gurumin::cameraTown(game<int>(0xf32c78),
          reinterpret_cast<const char*>(gameBase+0x527110),64)) return false;
  if (game<int>(0x1264a34) != 0 || manualBookActive()) return false;
  return reinterpret_cast<ManualCameraBlockerFn>(gameBase + 0x335670)() == 0 ||
         (fixedGameplayOrbit() && (establishedOrbit ||
           (hudSeen && frames - lastHudFrame <= 10)));

}
static bool cameraEligible() {
  return orbitCameraOwned() && hudSeen && frames - lastHudFrame <= 10 &&
         gameWindow && GetForegroundWindow() == gameWindow &&
         !game<int>(0x515ec4) && game<int>(0x791ec0) != 0;
}
static void resetCameraControls(bool discardPitch = false) {
  cameraSample = {};
  orbitControls.suspend(discardPitch);
  if (discardPitch) {
    fixedOrbitEngaged = false;
    fixedOrbitYaw = 0;
    orbitCameraKind = -1;
    orbitRenderPose = {};
    establishedOrbit = false;
  }
  cameraPitchScope = false;
}
static void __cdecl cameraInputHook(int *state) {
  nativeInput(state);
  cameraSample.valid = false;
  if (!cameraEligible() || !game<int>(0x1e280d4))
    return;
  auto reader = game<NativeXInputFn>(0x1e280cc);
  DWORD device = game<DWORD>(0x1e280e4);
  if (!reader)
    return;
  XINPUT_STATE input{};
  if (reader(device, &input) != ERROR_SUCCESS) {
    orbitControls.suspend(false);
    return;
  }
  if (cameraSample.device != device || cameraSample.reader != reader)
    orbitControls.suspend(false);
  cameraSample.device = device;
  cameraSample.reader = reader;
  cameraSample.time = nowSeconds();
  cameraSample.scene = game<int>(0xf32c78);
  cameraSample.stick =
      gurumin::radialStick(input.Gamepad.sThumbRX, input.Gamepad.sThumbRY,
                           modernSettings.cameraDeadzone);
  cameraSample.valid = true;
  // The right stick now belongs to orbit, so legacy axis bindings must not
  // apply a second rotation. Native buttons, left stick and menu input remain.
  if (state) {
    state[3] = 0x8000;
    state[4] = 0x8000;
  }
}
using CameraSegmentFn = unsigned(__thiscall *)(void *, const char *, unsigned,
                                               const float *, const float *,
                                               float *, float *);
static CameraSegmentFn cameraSegment;
static void constrainCameraOutput(float *eye, const float *target) {
  auto actor = game<char *>(0x1264684);
  if (!cameraSegment || !actor || !eye || !target) return;
  auto geometry = *reinterpret_cast<void **>(actor + 0x231c);
  if (!geometry) return;
  float hit[4]{}, distance = 999999.f;
  if (cameraSegment(geometry, nullptr, 0, eye, target, hit, &distance)) {
    float before[4]; memcpy(before, eye, sizeof(before));
    if (gurumin::contractCamera(eye, target, hit, 30.f) && diagnostics) {
      static unsigned traced;
      if (traced++ < 30 || frames % 600 == 0)
        log("Camera constraint distance=%g eye=%g,%g,%g -> %g,%g,%g "
            "target=%g,%g,%g hit=%g,%g,%g", distance,
            before[0],before[1],before[2],eye[0],eye[1],eye[2],
            target[0],target[1],target[2],hit[0],hit[1],hit[2]);
    }
  }
}
static bool cameraOutputsIndependent(float *eye, float *target) {
  if(!eye || !target) return false;
  uintptr_t a=uintptr_t(eye), b=uintptr_t(target);
  if((a<=b ? b-a : a-b)<16) return false;
  // Verified supported PE SizeOfImage. Native caller uses separate stack
  // vectors; reject any output overlapping the image's tracking globals.
  auto imageOverlap=[](uintptr_t p) {
    return p<gameBase ? gameBase-p<16 : p-gameBase<0x264a000u;
  };
  return !imageOverlap(a) && !imageOverlap(b);
}
static unsigned __cdecl cameraUpdateHook(float *eyeOut, float *targetOut) {
  orbitRenderPose={};
  int scene=game<int>(0xf32c78);
  char mapName[64]{};
  const char *nativeMap=reinterpret_cast<const char*>(gameBase+0x527110);
  for(unsigned i=0;i<sizeof(mapName)-1 && nativeMap[i];++i) mapName[i]=nativeMap[i];
  if(orbitScene!=scene || memcmp(orbitMapName,mapName,sizeof(mapName))) {
    orbitScene=scene;
    memcpy(orbitMapName,mapName,sizeof(mapName));
    resetCameraControls(true);
  }
  if(manualBookActive()) {
    resetCameraControls(false);
    return nativeCamera(eyeOut,targetOut);
  }
  if(!orbitCameraOwned()) {
    resetCameraControls(true);
    return nativeCamera(eyeOut,targetOut);
  }
  bool fixed=fixedGameplayOrbit();
  int kind=fixed?1:0;
  if(orbitCameraKind>=0 && orbitCameraKind!=kind) resetCameraControls(true);
  orbitCameraKind=kind;
  establishedOrbit=true;
  bool eligible=cameraEligible();
  bool fresh=cameraSample.valid && cameraSample.scene==scene &&
      nowSeconds()-cameraSample.time<=.1 && game<int>(0x1e280d4) &&
      game<DWORD>(0x1e280e4)==cameraSample.device &&
      game<NativeXInputFn>(0x1e280cc)==cameraSample.reader;
  bool firstEngagement=false;
  if(fixed && !fixedOrbitEngaged) {
    if(!eligible || !fresh ||
        (cameraSample.stick.x==0 && cameraSample.stick.y==0)) {
      orbitControls.suspend(true);
      return nativeCamera(eyeOut,targetOut);
    }
    fixedOrbitEngaged=true;
    firstEngagement=true;
    orbitControls.suspend(false);
  }
  float priorPitch=orbitControls.pitch, yaw=0;
  if(eligible && fresh)
    yaw=orbitControls.advance(game<unsigned>(0x1de47f4),cameraSample.stick,
                             modernSettings);
  else {
    cameraSample.valid=false;
    orbitControls.suspend(false);
  }
  if(fixed) {
    constexpr float fullTurn=6.283185307179586f;
    fixedOrbitYaw=std::remainder(fixedOrbitYaw+yaw,fullTurn);
  } else if(eligible && fresh) {
    auto eye=reinterpret_cast<float*>(gameBase+0x1d7b5b0);
    auto target=reinterpret_cast<float*>(gameBase+0x1ae4900);
    if(yaw!=0) gurumin::orbitYaw(eye,target,yaw);
    if(cameraSample.stick.x!=0 || cameraSample.stick.y!=0)
      game<int>(0x1264454)=2;
  }
  bool oldScope=cameraPitchScope;
  cameraPitchScope=!fixed;
  // A30 remains authored. Native tracking/transition/carry updates run even
  // after engagement; the offset is substituted at the by-value view builder.
  unsigned result=nativeCamera(eyeOut,targetOut);
  cameraPitchScope=oldScope;
  bool independent=cameraOutputsIndependent(eyeOut,targetOut);
  bool interior=gurumin::cameraInterior(scene,nativeMap,64);
  bool lower=eligible && fresh && !firstEngagement &&
      gurumin::loweringOrbit(cameraSample.stick,modernSettings);
  if(result && fixed && independent) {
    memcpy(orbitRenderPose.nativeEye.v,eyeOut,16);
    memcpy(orbitRenderPose.nativeTarget.v,targetOut,16);
    orbitRenderPose.eye=orbitRenderPose.nativeEye;
    orbitRenderPose.target=orbitRenderPose.nativeTarget;
    float acceptedPitch=orbitControls.pitch;
    bool adjusted=gurumin::orbitFromAnchor(orbitRenderPose.eye.v,
        orbitRenderPose.target.v,fixedOrbitYaw,orbitControls.pitch,&acceptedPitch);
    if(adjusted) {
      if(eligible && fresh && priorPitch!=orbitControls.pitch)
        orbitControls.pitch=acceptedPitch;
      if(std::abs(orbitControls.pitch)>.0001f && lower)
        constrainCameraOutput(orbitRenderPose.eye.v,orbitRenderPose.target.v);
      orbitRenderPose.tick=game<unsigned>(0x1de47f4);
      orbitRenderPose.scene=scene;
      orbitRenderPose.prepared=true;
    }
    if(diagnostics && (firstEngagement || frames%600==0))
      log("Authored orbit scene=%d offsetYaw=%g offsetPitch=%g "
          "nativeEye=%g,%g,%g renderEye=%g,%g,%g target=%g,%g,%g",
          scene,fixedOrbitYaw,orbitControls.pitch,eyeOut[0],eyeOut[1],eyeOut[2],
          orbitRenderPose.eye.v[0],orbitRenderPose.eye.v[1],orbitRenderPose.eye.v[2],
          targetOut[0],targetOut[1],targetOut[2]);
  } else if(result && independent && std::abs(orbitControls.pitch)>.0001f &&
            (interior || lower)) {
    // Preserve the established indoor native-manual camera adapter.
    constrainCameraOutput(eyeOut,targetOut);
  }
  static unsigned traced;
  if(diagnostics && (traced++<30 || frames%600==0))
    log("Orbit tick=%u authored=%d yaw=%g userPitch=%g nativePitch=%g "
        "stick=%g,%g device=%lu",
        game<unsigned>(0x1de47f4),fixed,yaw,orbitControls.pitch,
        game<float>(0x1264440),cameraSample.stick.x,cameraSample.stick.y,
        (unsigned long)cameraSample.device);
  return result;
}
static float *__cdecl cameraViewHook(float *out,CameraVector eye,
    CameraVector target,float *axes,float roll) {
  uintptr_t caller=uintptr_t(__builtin_return_address(0))-gameBase;
  bool use=caller==0x1a992a && orbitRenderPose.prepared && orbitCameraOwned() &&
      orbitRenderPose.scene==game<int>(0xf32c78) &&
      orbitRenderPose.tick==game<unsigned>(0x1de47f4) &&
      !memcmp(eye.v,orbitRenderPose.nativeEye.v,16) &&
      !memcmp(target.v,orbitRenderPose.nativeTarget.v,16);
  // By-value substitution leaves the caller's stack vectors untouched. It
  // later commits those original vectors back to native carry state.
  float *result=nativeCameraView(out,use?orbitRenderPose.eye:eye,
      use?orbitRenderPose.target:target,axes,roll);
  if(use && result) {
    memcpy(orbitRenderPose.view,result,64);
    orbitRenderPose.built=true;
  }
  return result;
}
static void __cdecl cameraWorldHook() {
  auto eye=reinterpret_cast<float*>(gameBase+0x1cf17a0);
  auto target=reinterpret_cast<float*>(gameBase+0x198f690);
  bool use=orbitRenderPose.built && orbitCameraOwned() &&
      orbitRenderPose.scene==game<int>(0xf32c78) &&
      !memcmp(eye,orbitRenderPose.nativeEye.v,16) &&
      !memcmp(target,orbitRenderPose.nativeTarget.v,16) &&
      !memcmp(reinterpret_cast<void*>(gameBase+0x9a9b00),orbitRenderPose.view,64);
  if(!use) { nativeCameraWorld(); return; }
  CameraVector savedEye,savedTarget;
  memcpy(savedEye.v,eye,16);memcpy(savedTarget.v,target,16);
  memcpy(eye,orbitRenderPose.eye.v,16);memcpy(target,orbitRenderPose.target.v,16);
  nativeCameraWorld();
  // Restore only our publication; preserve any legitimate native replacement.
  bool unchanged=!memcmp(eye,orbitRenderPose.eye.v,16) &&
                 !memcmp(target,orbitRenderPose.target.v,16);
  if(unchanged) { memcpy(eye,savedEye.v,16);memcpy(target,savedTarget.v,16); }
  else orbitRenderPose={};
}
static bool cameraMovementOutputsIndependent(float *x,float *y) {
  if(!x || !y) return false;
  uintptr_t a=uintptr_t(x),b=uintptr_t(y);
  if((a<=b?b-a:a-b)<sizeof(float)) return false;
  auto imageOverlap=[](uintptr_t p) {
    return p<gameBase ? gameBase-p<sizeof(float) : p-gameBase<0x264a000u;
  };
  return !imageOverlap(a) && !imageOverlap(b);
}
static unsigned __cdecl cameraMovementHook(float *outX,float *outY,
                                           float stickX,float stickY) {
  uintptr_t caller=uintptr_t(__builtin_return_address(0))-gameBase;
  // Run native anchor bookkeeping, input recording and carry updates first.
  // Replace its blended direction only for an established freecam pose.
  unsigned result=nativeCameraMovement(outX,outY,stickX,stickY);
  if(caller!=0x21d499 || !orbitCameraOwned() ||
      game<int>(0x515ec4) || !game<int>(0x791ec0) || !fixedGameplayOrbit() ||
      !fixedOrbitEngaged || orbitCameraKind!=1 || !orbitRenderPose.built ||
      orbitRenderPose.scene!=game<int>(0xf32c78) ||
      unsigned(game<unsigned>(0x1de47f4)-orbitRenderPose.tick)>1 ||
      !cameraMovementOutputsIndependent(outX,outY) ||
      (stickX==0 && stickY==0)) return result;
  const char *map=reinterpret_cast<const char*>(gameBase+0x527110);
  // Use the latest built simulation pose, including the preceding tick when
  // movement runs before the next camera build. Reject resource/scene switches.
  if(strncmp(map,orbitMapName,sizeof(orbitMapName)) ||
      orbitMapName[0]==0 ||
      memcmp(reinterpret_cast<void*>(gameBase+0x1cf17a0),orbitRenderPose.nativeEye.v,16) ||
      memcmp(reinterpret_cast<void*>(gameBase+0x198f690),orbitRenderPose.nativeTarget.v,16) ||
      memcmp(reinterpret_cast<void*>(gameBase+0x9a9b00),orbitRenderPose.view,64))
    return result;
  float x,y;
  if(!gurumin::cameraRelativeMovement(orbitRenderPose.eye.v,
      orbitRenderPose.target.v,stickX,stickY,x,y)) return result;
  *outX=x; *outY=y;
  if(diagnostics) {
    static unsigned traced;
    if(traced++<12 || frames%600==0)
      log("Camera-relative movement scene=%d poseTick=%u tick=%u stick=%g,%g world=%g,%g",
          orbitRenderPose.scene,orbitRenderPose.tick,game<unsigned>(0x1de47f4),
          stickX,stickY,x,y);
  }
  return result;
}
static void __fastcall cameraPitchHook(float *vector, void *,
                                       float nativeAngle) {
  uintptr_t caller = uintptr_t(__builtin_return_address(0)) - gameBase;
  if (cameraPitchScope && orbitControls.pitch != 0 &&
      gurumin::cameraPitchCall(caller))
    nativeAngle = gurumin::cameraPitchAngle(vector[1], vector[2], nativeAngle,
        orbitControls.pitch, caller == 0x314164 || caller == 0x3141e7);
  nativePitch(vector, nativeAngle);
}
static void installCameraHooks() {
  if (!supported || !modernSettings.freeCamera || cameraHooksActive)
    return;
  void *targets[] = {reinterpret_cast<void *>(gameBase + 0x3ae330),
                     reinterpret_cast<void *>(gameBase + 0x312f90),
                     reinterpret_cast<void *>(gameBase + 0x379200),
                     reinterpret_cast<void *>(gameBase + 0x37aea0),
                     reinterpret_cast<void *>(gameBase + 0x1d3360),
                     reinterpret_cast<void *>(gameBase + 0x32cb60)};
  void *detours[] = {reinterpret_cast<void *>(cameraInputHook),
                     reinterpret_cast<void *>(cameraUpdateHook),
                     reinterpret_cast<void *>(cameraPitchHook),
                     reinterpret_cast<void *>(cameraViewHook),
                     reinterpret_cast<void *>(cameraWorldHook),
                     reinterpret_cast<void *>(cameraMovementHook)};
  void **originals[] = {reinterpret_cast<void **>(&nativeInput),
                        reinterpret_cast<void **>(&nativeCamera),
                        reinterpret_cast<void **>(&nativePitch),
                        reinterpret_cast<void **>(&nativeCameraView),
                        reinterpret_cast<void **>(&nativeCameraWorld),
                        reinterpret_cast<void **>(&nativeCameraMovement)};
  unsigned created = 0;
  bool ok = true;
  for (unsigned i = 0; i < sizeof(targets)/sizeof(targets[0]); ++i) {
    auto status = MH_CreateHook(targets[i], detours[i], originals[i]);
    log("Optional camera hook RVA=%lx status=%d",
        (unsigned long)(uintptr_t(targets[i]) - gameBase), status);
    if (status != MH_OK) {
      ok = false;
      break;
    }
    ++created;
  }
  if (ok)
    for (void *target : targets)
      if (MH_EnableHook(target) != MH_OK) {
        ok = false;
        break;
      }
  if (!ok) {
    for (unsigned i = 0; i < created; ++i) {
      MH_DisableHook(targets[i]);
      MH_RemoveHook(targets[i]);
    }
    log("Optional camera unavailable; existing display hooks retained");
    return;
  }
  cameraSegment = reinterpret_cast<CameraSegmentFn>(gameBase + 0x361d30);
  cameraHooksActive = true;
  orbitScene = game<int>(0xf32c78);
  establishedOrbit = false;
  resetCameraControls(true);
  log("Steam Input right-stick orbit enabled");
}
