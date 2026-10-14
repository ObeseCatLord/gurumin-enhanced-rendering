#pragma once
// Portable VR math: OpenXR LOCAL space (Y up, -Z forward, metres) to the
// game's world (Z up, X/Y ground, game units). No Windows or D3D types.
#include "vr_protocol.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace gurumin::vr {
constexpr float Pi = 3.14159265358979323846f;

inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(Vec3 a, Vec3 b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline bool finite(Vec3 v) {
  return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
inline bool finite(Quat q) {
  return std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z) &&
         std::isfinite(q.w);
}
inline bool finite(Pose p) { return finite(p.q) && finite(p.p); }
inline Quat normalize(Quat q) {
  float n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
  if (!(n > 1e-6f) || !std::isfinite(n))
    return {};
  return {q.x / n, q.y / n, q.z / n, q.w / n};
}
inline Quat operator*(Quat a, Quat b) {
  return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
          a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
          a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
          a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}
inline Vec3 rotate(Quat q, Vec3 v) {
  Vec3 u{q.x, q.y, q.z};
  Vec3 t = cross(u, v) * 2.f;
  return v + t * q.w + cross(u, t);
}
// Rotation about OpenXR +Y; yaw(a) maps forward (0,0,-1) to (-sin a,0,-cos a).
inline Quat yawRotation(float radians) {
  return {0, std::sin(radians * .5f), 0, std::cos(radians * .5f)};
}
inline Quat pitchRotation(float radians) {
  return {std::sin(radians * .5f), 0, 0, std::cos(radians * .5f)};
}
// Heading of a pose's forward direction about +Y, or fallback when vertical.
inline float heading(Quat q, float fallback = 0) {
  Vec3 f = rotate(normalize(q), {0, 0, -1});
  if (f.x * f.x + f.z * f.z < 1e-6f)
    return fallback;
  return std::atan2(-f.x, -f.z);
}
inline float wrapAngle(float a) { return std::remainder(a, 2 * Pi); }

// Row-vector 4x4 (v' = v * M), element [row*4+col], the game's D3D layout.
inline void multiply(const float *a, const float *b, float *out) {
  float r[16]{};
  for (int row = 0; row < 4; ++row)
    for (int col = 0; col < 4; ++col)
      for (int k = 0; k < 4; ++k)
        r[row * 4 + col] += a[row * 4 + k] * b[k * 4 + col];
  std::memcpy(out, r, sizeof(r));
}
// Off-centre LH projection in the game's convention (w = view z). Depth
// terms p10/p14 come from the native pass projection so near/far match.
inline bool projection(const Fov &f, float p10, float p14, float *out) {
  float l = std::tan(f.left), r = std::tan(f.right), u = std::tan(f.up),
        d = std::tan(f.down);
  if (!(r > l) || !(u > d) || !std::isfinite(l + r + u + d) ||
      !std::isfinite(p10) || !std::isfinite(p14))
    return false;
  std::fill(out, out + 16, 0.f);
  out[0] = 2.f / (r - l);
  out[5] = 2.f / (u - d);
  out[8] = -(r + l) / (r - l);
  out[9] = -(u + d) / (u - d);
  out[10] = p10;
  out[11] = 1;
  out[14] = p14;
  return true;
}
// View for a camera at `position` with right/up/forward world axes.
inline void viewMatrix(Vec3 position, Vec3 right, Vec3 up, Vec3 forward,
                       float *out) {
  const Vec3 axes[3] = {right, up, forward};
  for (int col = 0; col < 3; ++col) {
    out[0 * 4 + col] = col == 0 ? right.x : col == 1 ? up.x : forward.x;
    out[1 * 4 + col] = col == 0 ? right.y : col == 1 ? up.y : forward.y;
    out[2 * 4 + col] = col == 0 ? right.z : col == 1 ? up.z : forward.z;
    out[3 * 4 + col] = -dot(position, axes[col]);
  }
  out[3] = out[7] = out[11] = 0;
  out[15] = 1;
}

struct RigConfig {
  float unitsPerMeter = 100;   // Game units per real metre at life size.
  float dioramaScale = 12;     // Diorama shrinks the world by this factor.
  float lifeDistance = 2.2f, lifeHeight = 1.5f;
  float dioramaDistance = .45f, dioramaHeight = .25f;
  float yawSpeed = 120 * Pi / 180;   // rad/s at full stick
  float elevationSpeed = 60 * Pi / 180;
  float horizontalTime = .06f, verticalTime = .35f; // anchor smoothing (s)
  float snapUnits = 400;       // Anchor jump treated as a teleport.
  bool diorama = false, snapTurn = false, invertX = true, invertY = false;
  float snapAngle = 30 * Pi / 180;
};

// Smoothly follows the player position: light horizontal smoothing, slower
// vertical follow so jumps do not bob the whole world. Teleports snap.
struct AnchorFilter {
  Vec3 value{};
  bool initialized = false;
  void reset() { initialized = false; }
  Vec3 update(Vec3 target, float dt, const RigConfig &c) {
    if (!finite(target))
      return value;
    Vec3 delta = target - value;
    if (!initialized || !std::isfinite(dt) || dt <= 0 || dt > .5f ||
        dot(delta, delta) > c.snapUnits * c.snapUnits) {
      value = target;
      initialized = true;
      return value;
    }
    float h = 1 - std::exp(-dt / std::max(c.horizontalTime, 1e-3f));
    float v = 1 - std::exp(-dt / std::max(c.verticalTime, 1e-3f));
    value.x += delta.x * h;
    value.y += delta.y * h;
    value.z += delta.z * v;
    return value;
  }
};

// The camera rig: maps recentered tracking space onto the game world around
// an anchor (the player), with a user-controlled yaw and elevation.
struct Rig {
  RigConfig config;
  float yaw = 0;          // Game yaw of the rig's forward (radians, X axis = 0).
  float elevation = 0;    // Angle of the anchor below the head, radians.
  float radius = 0;       // Metres from head to anchor (tracking space).
  Vec3 recenterPosition{};
  float recenterYaw = 0;
  bool recentered = false, oriented = false;
  float snapLatch = 0;
  Vec3 anchor{};          // Game units.

  float unitsPerMeter() const {
    float k = std::max(config.unitsPerMeter, 1e-3f);
    return config.diorama ? k * std::max(config.dioramaScale, 1e-3f) : k;
  }
  void resetFraming() {
    float d = config.diorama ? config.dioramaDistance : config.lifeDistance;
    float h = config.diorama ? config.dioramaHeight : config.lifeHeight;
    radius = std::max(std::hypot(d, h), .05f);
    elevation = std::atan2(h, d);
  }
  void recenter(const Pose &head) {
    recenterPosition = head.p;
    recenterYaw = heading(head.q, recenterYaw);
    recentered = true;
  }
  void setYawFromForward(float x, float y) {
    if (x * x + y * y > 1e-8f) {
      yaw = std::atan2(y, x);
      oriented = true;
    }
  }
  // Stick x/y in -1..1 (+y up). Smooth orbit or snap turns; y raises the rig.
  void steer(float x, float y, float dt) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(dt))
      return;
    dt = std::clamp(dt, 0.f, .1f);
    // Same meaning as the desktop orbit: with Invert X (the default) pushing
    // right turns the view right.
    float turn = config.invertX ? x : -x;
    if (config.snapTurn) {
      if (std::abs(turn) < .3f)
        snapLatch = 0;
      else if (snapLatch == 0 && std::abs(turn) > .7f) {
        snapLatch = turn > 0 ? 1.f : -1.f;
        yaw = wrapAngle(yaw - snapLatch * config.snapAngle);
      }
    } else
      yaw = wrapAngle(yaw - turn * config.yawSpeed * dt);
    float lift = config.invertY ? -y : y;
    elevation = std::clamp(elevation + lift * config.elevationSpeed * dt,
                           -10 * Pi / 180, 80 * Pi / 180);
  }
  // Recentered tracking vector -> game direction (no translation or scale).
  Vec3 direction(Vec3 t) const {
    float c = std::cos(yaw), s = std::sin(yaw);
    Vec3 forward{c, s, 0}, right{s, -c, 0}, up{0, 0, 1};
    return right * t.x + up * t.y + forward * (-t.z);
  }
  Vec3 recenterVector(Vec3 p) const {
    return rotate(yawRotation(-recenterYaw), p - recenterPosition);
  }
  Quat recenterRotation(Quat q) const {
    return normalize(yawRotation(-recenterYaw) * normalize(q));
  }
  // Anchor sits below and ahead of the recentered head.
  Vec3 anchorOffset() const {
    return {0, -radius * std::sin(elevation), -radius * std::cos(elevation)};
  }
  Vec3 worldPosition(Vec3 tracking) const {
    return anchor + direction(recenterVector(tracking) - anchorOffset()) *
                        unitsPerMeter();
  }
  // Game view matrix for a tracked eye.
  void eyeView(const Pose &eye, float *out) const {
    Quat q = recenterRotation(eye.q);
    viewMatrix(worldPosition(eye.p), direction(rotate(q, {1, 0, 0})),
               direction(rotate(q, {0, 1, 0})), direction(rotate(q, {0, 0, -1})),
               out);
  }
  // Horizontal world gaze of the head (for movement), false when vertical.
  bool gaze(const Pose &head, float &x, float &y) const {
    Vec3 f = direction(rotate(recenterRotation(head.q), {0, 0, -1}));
    float length = std::hypot(f.x, f.y);
    if (!(length > 1e-3f))
      return false;
    x = f.x / length;
    y = f.y / length;
    return true;
  }
};

// Host-side HUD placement: follows the head softly, trailing behind turns
// and movement, without roll. Runs in OpenXR LOCAL space.
struct SoftFollow {
  Vec3 position{};
  float yaw = 0, pitch = 0;
  bool initialized = false;
  float angleTime = .3f, positionTime = .15f;
  void reset() { initialized = false; }
  void update(const Pose &head, float dt) {
    if (!finite(head))
      return;
    Vec3 f = rotate(normalize(head.q), {0, 0, -1});
    float targetYaw = heading(head.q, yaw);
    float targetPitch =
        std::asin(std::clamp(f.y, -1.f, 1.f)) * .85f; // ease off extremes
    if (!initialized || !std::isfinite(dt) || dt <= 0 || dt > .5f) {
      position = head.p;
      yaw = targetYaw;
      pitch = targetPitch;
      initialized = true;
      return;
    }
    float a = 1 - std::exp(-dt / angleTime);
    float p = 1 - std::exp(-dt / positionTime);
    yaw = wrapAngle(yaw + wrapAngle(targetYaw - yaw) * a);
    pitch += (targetPitch - pitch) * a;
    position = position + (head.p - position) * p;
  }
  // `drop` pitches the panel below the followed gaze (radians).
  Pose panel(float distance, float drop = 0) const {
    Quat q = normalize(yawRotation(yaw) * pitchRotation(pitch - drop));
    return {q, position + rotate(q, {0, 0, -distance})};
  }
};
} // namespace gurumin::vr
