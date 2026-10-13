#include "../src/vr_math.hpp"
#include <cassert>
#include <cstdio>
using namespace gurumin::vr;
static bool near(float a, float b, float e = 1e-3f) { return std::abs(a - b) <= e; }
static void transform(const float *m, Vec3 v, float out[4]) {
  float in[4] = {v.x, v.y, v.z, 1};
  for (int col = 0; col < 4; ++col) {
    out[col] = 0;
    for (int k = 0; k < 4; ++k)
      out[col] += in[k] * m[k * 4 + col];
  }
}
static float det3(const float *m) {
  return m[0] * (m[5] * m[10] - m[6] * m[9]) - m[1] * (m[4] * m[10] - m[6] * m[8]) +
         m[2] * (m[4] * m[9] - m[5] * m[8]);
}
int main() {
  Rig rig;
  rig.resetFraming();
  rig.anchor = {1000, -500, 30};
  rig.yaw = 0.3f;
  Pose head{normalize(yawRotation(0.7f)), {0.3f, 1.6f, 0.2f}};
  rig.recenter(head);
  // The recentered head sits behind and above the anchor along rig forward.
  Vec3 eye = rig.worldPosition(head.p);
  float k = rig.unitsPerMeter();
  Vec3 forward{std::cos(rig.yaw), std::sin(rig.yaw), 0};
  Vec3 toAnchor = rig.anchor - eye;
  assert(near(toAnchor.z, -rig.config.lifeHeight * k, .05f));
  assert(near(dot(toAnchor, forward), rig.config.lifeDistance * k, .05f));
  assert(near(toAnchor.x * forward.y - toAnchor.y * forward.x, 0, .05f));

  // A point one metre straight ahead of the recentered head is centred at
  // view depth k; right and up map to positive view x and y.
  float view[16], proj[16], vp[16];
  rig.eyeView(head, view);
  assert(det3(view) < 0); // Same handedness reflection as Falcom's camera.
  float out[4];
  transform(view, eye + forward * k, out);
  assert(near(out[0], 0, .01f) && near(out[1], 0, .01f) && near(out[2], k, .01f));
  Vec3 right{std::sin(rig.yaw), -std::cos(rig.yaw), 0};
  transform(view, eye + forward * k + right * 10.f, out);
  assert(out[0] > 9.9f);
  transform(view, eye + forward * k + Vec3{0, 0, 10}, out);
  assert(out[1] > 9.9f);

  // Off-centre projection reaches NDC +-1 at the FOV edges.
  Fov fov{-0.9f, 0.7f, 0.8f, -0.85f};
  assert(projection(fov, 1.001f, -10.01f, proj));
  multiply(view, proj, vp);
  Vec3 edge = eye + forward * k + right * (k * std::tan(fov.right));
  transform(vp, edge, out);
  assert(near(out[0] / out[3], 1, 1e-3f) && out[3] > 0);
  edge = eye + forward * k + Vec3{0, 0, k * std::tan(fov.down)};
  transform(vp, edge, out);
  assert(near(out[1] / out[3], -1, 1e-3f));

  // Head turned 90 degrees left (OpenXR +yaw) gazes to the rig's left.
  Pose left{normalize(yawRotation(0.7f + Pi / 2)), head.p};
  float gx, gy;
  assert(rig.gaze(left, gx, gy));
  assert(near(gx, -right.x, 1e-3f) && near(gy, -right.y, 1e-3f));
  assert(rig.gaze(head, gx, gy) && near(gx, forward.x) && near(gy, forward.y));

  // Invert X (default): pushing right turns the view right.
  float before = rig.yaw;
  rig.steer(1, 0, 0.05f);
  assert(rig.yaw < before);
  rig.config.invertX = false;
  before = rig.yaw;
  rig.steer(1, 0, 0.05f);
  assert(rig.yaw > before);
  // Pushing up raises the rig (anchor further below); elevation is bounded.
  float elevation = rig.elevation;
  rig.steer(0, 1, 0.05f);
  assert(rig.elevation > elevation);
  for (int i = 0; i < 1000; ++i) rig.steer(0, 1, 0.1f);
  assert(rig.elevation <= 80 * Pi / 180 + 1e-5f);
  // Snap turns fire once per push.
  rig.config.snapTurn = true;
  rig.config.invertX = true;
  before = rig.yaw;
  rig.steer(1, 0, 0.01f);
  rig.steer(1, 0, 0.01f);
  assert(near(wrapAngle(before - rig.yaw), rig.config.snapAngle));
  rig.steer(0, 0, 0.01f);
  rig.steer(1, 0, 0.01f);
  assert(near(wrapAngle(before - rig.yaw), 2 * rig.config.snapAngle));

  // Diorama makes the world smaller: more game units per real metre.
  Rig diorama;
  diorama.config.diorama = true;
  assert(near(diorama.unitsPerMeter(), 1200));

  // Anchor filter: snaps first and on teleports, eases vertical motion.
  AnchorFilter filter;
  RigConfig config;
  Vec3 a = filter.update({0, 0, 0}, 0.01f, config);
  assert(a.x == 0 && filter.initialized);
  a = filter.update({10, 0, 50}, 0.011f, config);
  assert(a.x > 1 && a.x < 10 && a.z > 0 && a.z < 5);
  a = filter.update({5000, 0, 0}, 0.011f, config);
  assert(a.x == 5000);

  // HUD follow trails a head turn and converges.
  SoftFollow follow;
  follow.update({{}, {0, 1.6f, 0}}, 0.011f);
  Pose turned{normalize(yawRotation(1.0f)), {0, 1.6f, 0}};
  follow.update(turned, 0.011f);
  assert(follow.yaw > 0 && follow.yaw < 0.2f);
  for (int i = 0; i < 400; ++i) follow.update(turned, 0.011f);
  assert(near(follow.yaw, 1.0f, 1e-3f));
  Pose panel = follow.panel(1.4f);
  Vec3 f = rotate(turned.q, {0, 0, -1});
  assert(near(panel.p.x, f.x * 1.4f, 1e-2f) && near(panel.p.z, f.z * 1.4f, 1e-2f));
  // Collision pull-in: the head origin moves straight toward the anchor,
  // quickly when blocked and slowly back out.
  Rig pulled;
  pulled.resetFraming();
  pulled.anchor = {0, 0, 0};
  Vec3 full = pulled.originWorld(1), half = pulled.originWorld(.5f);
  assert(near(half.x, full.x * .5f, .01f) && near(half.z, full.z * .5f, .01f));
  pulled.settlePull(.4f, 0.1f);
  assert(pulled.pull < .5f);
  float blocked = pulled.pull;
  pulled.settlePull(1, 0.1f);
  assert(pulled.pull > blocked && pulled.pull < .6f);
  pulled.settlePull(.1f, 1);
  assert(pulled.pull >= .35f); // Never into the character.
  // Lifting raises the head origin while keeping its distance.
  Vec3 level = pulled.originWorld(1, 0), raised = pulled.originWorld(1, .5f);
  assert(raised.z > level.z);
  assert(near(std::sqrt(dot(raised, raised)), std::sqrt(dot(level, level)), .5f));
  std::puts("vr_math_test passed");
}
