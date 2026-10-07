#include "../src/settings.hpp"
#include <cassert>
#include <limits>
using namespace gurumin;
int main() {
  auto modes = resolutionChoices({3440, 1440}, {3440, 1440});
  assert(modes.size() == 15);
  for (Resolution r : {Resolution{2560, 1080},
                       {3440, 1440},
                       {3840, 1600},
                       {3840, 2160},
                       {5120, 1440}})
    assert(std::count(modes.begin(), modes.end(), r) == 1);
  assert(resolutionChoices({3000, 2000}, {3000, 2000}).size() == 16);
  assert(!validResolution({999999, 1440}) && !validResolution({3440, 0}));
  assert(validFrameCap(-1) && validFrameCap(0) && validFrameCap(175));
  assert(!validFrameCap(1) && !validFrameCap(-2) && !validFrameCap(1001));
  FramePacer pacer;
  double time = 1;
  for (int i = 0; i < 1750; ++i) {
    time = pacer.next(time, 175);
    assert(std::abs(time - (1. + (i + 1) / 175.)) < 1e-10);
  }
  assert(std::abs(pacer.next(time + 10., 60) - (time + 10. + 1. / 60.)) <
         1e-10);
  assert(pacer.next(100., 0) == 100. && pacer.deadline == 0);
  assert(pacer.next(100., -1) == 100.);
  pacer.reset();
  time = pacer.next(1., 175);
  time += .010; // Rendering slower than the target period.
  assert(pacer.next(time, 175) == time);
  time += .010;
  assert(pacer.next(time, 175) == time);
  assert(std::abs(pacer.next(time + .001, 175) - (time + 1. / 175.)) < 1e-10);
  float anchorEye[4]={10,0,5,7}, anchorTarget[4]={0,0,0,8};
  float nativeAnchor[4]; std::copy(anchorEye,anchorEye+4,nativeAnchor);
  assert(orbitFromAnchor(anchorEye,anchorTarget,0,0));
  assert(std::equal(anchorEye,anchorEye+4,nativeAnchor));
  float nativeRadius=std::hypot(10.f,5.f);
  assert(orbitFromAnchor(anchorEye,anchorTarget,float(acos(-1.)/2),.2f));
  assert(std::abs(anchorEye[0])<1e-5 && anchorEye[1]>0 && anchorEye[2]>5);
  assert(std::abs(std::hypot(std::hypot(anchorEye[0],anchorEye[1]),anchorEye[2])-nativeRadius)<1e-5);
  assert(anchorEye[3]==7 && anchorTarget[3]==8);
  float limitedEye[4]={10,0,5,1}, limitedPitch=3;
  assert(orbitFromAnchor(limitedEye,anchorTarget,0,3,&limitedPitch));
  assert(std::abs(std::atan2(limitedEye[2],std::hypot(limitedEye[0],limitedEye[1]))-1.3962634f)<1e-5);
  assert(limitedPitch<3);
  float invalidEye[4]={0,0,0,1};
  assert(!orbitFromAnchor(invalidEye,anchorTarget,1,.1f));
  ModernSettings direction; direction.invertY=false;
  assert(loweringOrbit({0,1},direction) && !loweringOrbit({0,-1},direction));
  assert(!loweringOrbit({1,0},direction)); direction.invertY=true;
  assert(loweringOrbit({0,-1},direction) && !loweringOrbit({0,1},direction));
  assert(radialStick(100, -100, 8689).x == 0);
  assert(radialStick(32767, 0, 8689).x == 1);
  assert(radialStick(-32768, 0, 8689).x == -1);
  auto diagonal = radialStick(32767, 32767, 8689);
  assert(std::abs(std::hypot(diagonal.x, diagonal.y) - 1.f) < 1e-6);
  assert(radialStick(0, 32767, 8689).y == 1);
  float eye[4] = {11, 2, 30, 1}, target[4] = {1, 2, 3, 1};
  assert(orbitYaw(eye, target, float(acos(-1.) / 2)));
  assert(std::abs(eye[0] - 1) < 1e-5 && std::abs(eye[1] - 12) < 1e-5);
  assert(eye[2] == 30 && eye[3] == 1 && target[0] == 1);
  assert(!orbitYaw(eye, target, std::numeric_limits<float>::quiet_NaN()));
  assert(cameraInterior(2,"mp_020",7));
  assert(cameraInterior(3,"MP_030.IT3",11));
  assert(cameraInterior(4,"mp_040",7));
  assert(cameraInterior(5,"mp_050",7));
  assert(!cameraInterior(2,"mp_030",7));
  assert(!cameraInterior(12,"mp_0C0",7));
  assert(!cameraInterior(102,"mp_102",7));
  assert(!cameraInterior(2,"mp_020x",8));
  assert(!cameraInterior(2,"mp_020",6));
  assert(!cameraInterior(2,nullptr,0));
  ModernSettings settings;
  assert(!settings.freeCamera && !settings.cameraInteriorsOnly && settings.invertX);
  settings.invertX=false; // Baseline rotation math is independent of defaults.
  OrbitControls orbit;
  assert(orbit.advance(10, {1, 1}, settings) == 0);
  float yaw = orbit.advance(11, {1, 1}, settings), pitch = orbit.pitch;
  for (int i = 0; i < 20; ++i) {
    assert(orbit.advance(11, {1, 1}, settings) == 0);
    assert(orbit.pitch == pitch);
  }
  assert(std::abs(yaw - float(acos(-1.) / 45.)) < 1e-6); // 120 deg/s at 30 Hz.
  assert(pitch < 0);
  assert(std::abs(orbit.advance(13, {1, 0}, settings) - 2 * yaw) < 1e-6);
  assert(orbit.advance(10000, {1, 1}, settings) == 0);
  orbit.suspend(false);
  assert(orbit.advance(10001, {1, 1}, settings) == 0 && orbit.pitch == pitch);
  orbit.suspend(true);
  assert(orbit.pitch == 0);
  settings.invertY = true;
  orbit.advance(1, {0, 1}, settings);
  orbit.advance(2, {0, 1}, settings);
  assert(orbit.pitch > 0);
  for (unsigned tick = 3; tick < 1000; ++tick)
    orbit.advance(tick, {0, 1}, settings);
  assert(orbit.pitch == 2.8f);
  // Identical stick input rotates by the same total at every presentation rate.
  for (int fps : {30, 60, 75, 120, 175, 360}) {
    OrbitControls run;
    float totalYaw = 0;
    for (int frame = 0; frame <= fps * 3; ++frame) {
      unsigned tick = unsigned(std::floor(frame * 30. / fps + 1e-9));
      totalYaw += run.advance(tick, {1, 0}, settings);
    }
    assert(std::abs(totalYaw - float(2 * acos(-1.))) < 1e-4);
  }
  // Wider geometric range, consistent collision probes and immediate reversal.
  float user = -3.f;
  float angle = cameraPitchAngle(1050, -650, .1f, user, true);
  float base = std::atan2(650.f,1050.f);
  assert(std::abs(base + angle + 1.3962634f) < 1e-5);
  assert(std::abs(cameraPitchAngle(1050,-650,.07f,user,false) + base) <= 1.396264f);
  float limitUser = user;
  user += .05f;
  assert(cameraPitchAngle(1050,-650,.1f,user,true) > angle && user > limitUser);
  // The final eye contracts towards an unchanged target; no minimum distance
  // forces it through the floor. Homogeneous coordinates remain untouched.
  float cameraEye[4]={100,0,-100,1}, cameraTarget[4]={0,0,100,1};
  float floorHit[4]={50,0,0,1};
  assert(contractCamera(cameraEye,cameraTarget,floorHit,30));
  assert(cameraEye[2] > 0 && cameraEye[0] < 50 && cameraEye[3] == 1);
  assert(cameraTarget[2] == 100);
  float behind[4]={-10,0,120,1};
  assert(!contractCamera(cameraEye,cameraTarget,behind,30));
  settings.invertX = true;
  OrbitControls inverted;
  inverted.advance(1, {1, 0}, settings);
  assert(std::abs(inverted.advance(2, {1, 0}, settings) + yaw) < 1e-6);
  for (uintptr_t caller :
       {0x314164u, 0x3141e7u, 0x3145bbu, 0x3149d1u, 0x314b80u})
    assert(cameraPitchCall(caller));
  assert(!cameraPitchCall(0x1a22fdu) && !cameraPitchCall(0x314163u));
  for(int edge : {256,512,1024,2048}) assert(validShadowResolution(edge));
  assert(!validShadowResolution(0) && !validShadowResolution(777));
  assert(validAntiAliasing(0) && validAntiAliasing(2) && !validAntiAliasing(3));
}
