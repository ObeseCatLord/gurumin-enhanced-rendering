#pragma once
// Named-section channel shared by the d3d9 proxy (creator) and the OpenXR
// host (opener). Adapted from the SS2VR host IPC (MIT, same author).
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include "vr_protocol.hpp"
#include <string>
#include <windows.h>

namespace gurumin::vr {
struct Channel {
  HANDLE mapping = nullptr, mutex = nullptr;
  Shared *shared = nullptr;
  std::wstring token;
  Channel() = default;
  Channel(const Channel &) = delete;
  Channel &operator=(const Channel &) = delete;
  ~Channel() { close(); }
  static std::wstring name(const std::wstring &id) {
    return L"Local\\GuruminVR-" + id;
  }
  bool open(const std::wstring &id, bool create) {
    close();
    token = id;
    auto n = name(id);
    mapping = create ? CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr,
                                          PAGE_READWRITE, 0, sizeof(Shared),
                                          n.c_str())
                     : OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, n.c_str());
    if (!mapping)
      return false;
    shared = static_cast<Shared *>(
        MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Shared)));
    mutex = create ? CreateMutexW(nullptr, FALSE, (n + L"-lock").c_str())
                   : OpenMutexW(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE,
                                (n + L"-lock").c_str());
    if (!shared || !mutex) {
      close();
      return false;
    }
    if (create) {
      ZeroMemory(shared, sizeof(Shared));
      shared->magic = Magic;
      shared->abi = Abi;
      shared->bytes = sizeof(Shared);
      shared->gamePid = GetCurrentProcessId();
      shared->game = GameState{};
    }
    return shared->magic == Magic && shared->abi == Abi &&
           shared->bytes == sizeof(Shared);
  }
  // Returns false for a timeout. An abandoned mutex means the peer died while
  // writing: the payload is untrusted, so the channel is marked shut down.
  bool lock(DWORD ms) {
    if (!mutex)
      return false;
    DWORD result = WaitForSingleObject(mutex, ms);
    if (result == WAIT_ABANDONED) {
      if (shared) {
        shared->error = 1;
        shared->shutdown = 1;
      }
      ReleaseMutex(mutex);
      return false;
    }
    return result == WAIT_OBJECT_0;
  }
  void unlock() { ReleaseMutex(mutex); }
  void close() {
    if (shared)
      UnmapViewOfFile(shared);
    shared = nullptr;
    for (HANDLE h : {mapping, mutex})
      if (h)
        CloseHandle(h);
    mapping = mutex = nullptr;
  }
};
struct Lock {
  Channel &channel;
  bool held;
  Lock(Channel &c, DWORD ms) : channel(c), held(c.lock(ms)) {}
  ~Lock() {
    if (held)
      channel.unlock();
  }
  Lock(const Lock &) = delete;
  Lock &operator=(const Lock &) = delete;
  explicit operator bool() const { return held; }
};
// One section per slot x image keeps every 32-bit view small.
struct PixelStore {
  HANDLE mapping[PixelBufferCount]{};
  uint8_t *view[PixelBufferCount]{};
  uint32_t generation = 0, width = 0, height = 0, hudWidth = 0, hudHeight = 0;
  PixelStore() = default;
  PixelStore(const PixelStore &) = delete;
  PixelStore &operator=(const PixelStore &) = delete;
  ~PixelStore() { close(); }
  static std::wstring name(const std::wstring &token, uint32_t generation,
                           unsigned buffer) {
    return Channel::name(token) + L"-px-" + std::to_wstring(generation) +
           L"-" + std::to_wstring(buffer);
  }
  uint64_t bytes(unsigned buffer) const {
    return buffer % ImageCount == HudImage ? imageBytes(hudWidth, hudHeight)
                                           : imageBytes(width, height);
  }
  bool matches(const PixelSections &p) const {
    return view[0] && generation == p.generation && width == p.width &&
           height == p.height && hudWidth == p.hudWidth &&
           hudHeight == p.hudHeight;
  }
  bool open(const std::wstring &token, const PixelSections &p, bool create) {
    close();
    if (!p.generation || !p.width || !p.height || !p.hudWidth ||
        !p.hudHeight || p.width > MaxDimension || p.height > MaxDimension ||
        p.hudWidth > MaxDimension || p.hudHeight > MaxDimension)
      return false;
    generation = p.generation;
    width = p.width;
    height = p.height;
    hudWidth = p.hudWidth;
    hudHeight = p.hudHeight;
    for (unsigned i = 0; i < PixelBufferCount; ++i) {
      uint64_t size = bytes(i);
      auto n = name(token, generation, i);
      mapping[i] =
          create ? CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr,
                                      PAGE_READWRITE, DWORD(size >> 32),
                                      DWORD(size), n.c_str())
                 : OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, n.c_str());
      view[i] = mapping[i] ? static_cast<uint8_t *>(MapViewOfFile(
                                 mapping[i], FILE_MAP_ALL_ACCESS, 0, 0,
                                 size_t(size)))
                           : nullptr;
      if (!view[i]) {
        close();
        return false;
      }
    }
    return true;
  }
  void close() {
    for (unsigned i = 0; i < PixelBufferCount; ++i) {
      if (view[i])
        UnmapViewOfFile(view[i]);
      if (mapping[i])
        CloseHandle(mapping[i]);
      view[i] = nullptr;
      mapping[i] = nullptr;
    }
    generation = width = height = hudWidth = hudHeight = 0;
  }
};
} // namespace gurumin::vr
