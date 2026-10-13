#pragma once
// Shared-memory contract between the x86 d3d9 proxy and the x64 OpenXR host.
// Layout is fixed with explicit padding so both compilers agree on every
// offset; the static_asserts below are checked by both builds.
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace gurumin::vr {
constexpr uint32_t Magic = 0x52565247; // "GRVR"
constexpr uint32_t Abi = 1;
constexpr uint32_t MaxDimension = 8192;
// Per slot: left eye, right eye, then the HUD / flat screen image.
constexpr unsigned ImageCount = 3;
enum ImageIndex : unsigned { LeftEye = 0, RightEye = 1, HudImage = 2 };
// Per-hand controller buttons, already abstracted from interaction profiles.
enum Button : uint32_t {
  Primary = 1,   // A (right) / X (left)
  Secondary = 2, // B (right) / Y (left)
  Thumb = 4,     // Stick or trackpad click
  Menu = 8,      // Menu / view button where the profile has one
};
// What a published slot carries.
enum Content : uint32_t {
  World = 1,  // Both eye images hold the stereo world.
  Hud = 2,    // HUD image is premultiplied overlay for the world.
  Screen = 4, // HUD image is an opaque flat screen (title, menus, book).
};
enum class SlotState : uint32_t { Empty, Requested, Rendering, Ready };

struct Vec3 {
  float x = 0, y = 0, z = 0;
};
struct Quat {
  float x = 0, y = 0, z = 0, w = 1;
};
struct Pose {
  Quat q;
  Vec3 p;
};
struct Fov {
  float left = 0, right = 0, up = 0, down = 0;
};
// Sampled by the host every XR frame. Poses are in the runtime's LOCAL space.
struct Input {
  uint64_t sequence = 0, tickMs = 0;
  uint32_t session = 0, focused = 0, headValid = 0, reserved = 0;
  Pose head;
  uint32_t pad0 = 0;
  float stick[2][2]{};   // [hand][x,y], -1..1, +y up
  float trigger[2]{};    // 0..1
  float squeeze[2]{};    // 0..1
  uint32_t buttons[2]{}; // Button bits per hand
};
struct Request {
  uint64_t sequence = 0;
  int64_t predictedTime = 0;
  uint32_t session = 0, width = 0, height = 0, reserved = 0;
  Pose eye[2];
  Fov fov[2];
  Input input;
};
struct Slot {
  SlotState state = SlotState::Empty;
  uint32_t cancelled = 0;
  uint32_t content = 0;
  // Zero-copy generation the pair was rendered into (0 = pixel sections).
  uint32_t sharedImage = 0;
  // Becomes 1 once the game observed its GPU work on the shared images.
  uint32_t gpuReady = 0;
  uint32_t pixelGeneration = 0;
  Request request;
};
// D3D9 (DXVK) KMT handles for slot x image, opened by the host per generation.
struct SharedImages {
  uint32_t generation = 0, width = 0, height = 0, hudWidth = 0, hudHeight = 0,
           opened = 0;
  uint32_t handle[2][ImageCount]{};
};
// Fallback CPU transfer: one named section per slot x image.
struct PixelSections {
  uint32_t generation = 0, width = 0, height = 0, hudWidth = 0, hudHeight = 0,
           reserved = 0;
};
// Published by the game; the host only requests frames while it is fresh.
struct GameState {
  uint64_t tickMs = 0;
  uint32_t rendering = 0, eyeWidth = 0, eyeHeight = 0, hudWidth = 0,
           hudHeight = 0, scaleMode = 0;
  float hudDistance = 1.4f, hudWidthMeters = 1.1f;
  float screenDistance = 2.2f, screenWidthMeters = 2.4f;
};
struct Shared {
  uint32_t magic = Magic, abi = Abi, bytes = sizeof(Shared), gamePid = 0;
  uint32_t hostPid = 0, shutdown = 0, error = 0, hostReady = 0;
  Input latest;
  Slot slot[2];
  uint32_t recommendedWidth = 0, recommendedHeight = 0;
  SharedImages images;
  PixelSections pixels;
  GameState game;
};
static_assert(sizeof(Vec3) == 12 && sizeof(Quat) == 16 && sizeof(Pose) == 28);
static_assert(sizeof(Input) == 104 && offsetof(Input, head) == 32);
static_assert(sizeof(Request) == 224 && offsetof(Request, input) == 120);
static_assert(sizeof(Slot) == 248 && offsetof(Slot, request) == 24);
static_assert(sizeof(SharedImages) == 48 && sizeof(PixelSections) == 24);
static_assert(sizeof(GameState) == 48);
static_assert(offsetof(Shared, latest) == 32 && offsetof(Shared, slot) == 136 &&
              offsetof(Shared, images) == 640 &&
              offsetof(Shared, game) == 712 && sizeof(Shared) == 760);
static_assert(std::is_trivially_copyable_v<Shared> &&
              std::is_standard_layout_v<Shared>);

constexpr uint64_t imageBytes(uint32_t width, uint32_t height) {
  return uint64_t(width) * height * 4;
}
// Pixel section name index for slot x image.
constexpr unsigned pixelBuffer(unsigned slot, unsigned image) {
  return slot * ImageCount + image;
}
constexpr unsigned PixelBufferCount = 2 * ImageCount;
// Requests older than this are not rendered; pairs older than it are dropped.
constexpr uint64_t FrameAgeMs = 150, GameStateAgeMs = 250;
} // namespace gurumin::vr
