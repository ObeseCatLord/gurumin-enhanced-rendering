# Third-party notices

- **MinHook 1.3.4**, Tsuda Kageyu; Hacker Disassembler Engine components by
  Vyacheslav Patkov. BSD-style licenses reproduced in
  [vendor/minhook/LICENSE.txt](vendor/minhook/LICENSE.txt).
- **NVIDIA FXAA 3.11**, Timothy Lottes / NVIDIA. BSD-3 license reproduced in
  [vendor/fxaa/LICENSE.txt](vendor/fxaa/LICENSE.txt). The source distribution used
  is [Intel GameTechDev/CMAA2](https://github.com/GameTechDev/CMAA2/tree/master/Projects/CMAA2/FXAA).
- **Khronos OpenXR SDK 1.1.53** (loader built as `GuruminVR/openxr_loader.dll`,
  headers in `vendor/openxr/include`), The Khronos Group Inc. Apache-2.0 (or MIT for
  some files); license reproduced in [vendor/openxr/LICENSE](vendor/openxr/LICENSE)
  and shipped as `GuruminVR/OpenXR-LICENSE.txt`. Source:
  [KhronosGroup/OpenXR-SDK](https://github.com/KhronosGroup/OpenXR-SDK) at
  `75c53b6e853dc12c7b3c771edc9c9c841b15faaa`.
- The VR host and IPC structure are adapted from the author's own SS2VR project
  (MIT).
- The release's Windows binaries link the MinGW-w64 runtime and libstdc++
  statically (GPLv3 with the GCC Runtime Library Exception).
- **Python 3.13.16 embeddable x64 runtime**, Python Software Foundation. Included
  only in the release ZIP's `runtime/` directory with its original `LICENSE.txt`.
  [Official download and SHA-256](https://www.python.org/downloads/release/python-31316/):
  `97dae5274cc54867065e8d5a3226e48c35017ed332a0fdb0e27d5b5821961297`.

These third-party licenses govern those components independently of this
project's MIT license. No Gurumin executable, texture, model, save or decompiled
source is distributed with this project or patch.
