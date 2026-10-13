#!/bin/sh
# Builds the official Khronos OpenXR loader (OpenXR-SDK 1.1.53) as a static-
# runtime x64 Windows DLL for the VR host. OPENXR_SOURCE may point at an
# existing checkout of that tag; otherwise it is cloned into build/.
set -eu
cd "$(dirname "$0")/.."
commit=75c53b6e853dc12c7b3c771edc9c9c841b15faaa
source=${OPENXR_SOURCE:-build/openxr-sdk}
if [ ! -f "$source/CMakeLists.txt" ]; then
  git clone --quiet https://github.com/KhronosGroup/OpenXR-SDK.git "$source"
  git -C "$source" checkout --quiet "$commit"
fi
cmake -S "$source" -B build/openxr-loader -G "Unix Makefiles" \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/tools/cmake/mingw64.cmake" \
  -DCMAKE_BUILD_TYPE=Release -DDYNAMIC_LOADER=ON -DBUILD_TESTS=OFF \
  -DBUILD_API_LAYERS=OFF -DBUILD_CONFORMANCE_TESTS=OFF \
  -DBUILD_WITH_SYSTEM_JSONCPP=OFF -DCMAKE_DISABLE_FIND_PACKAGE_JsonCpp=ON \
  -DCMAKE_SHARED_LINKER_FLAGS="-static -static-libgcc -static-libstdc++" >/dev/null
cmake --build build/openxr-loader --target openxr_loader -j"$(nproc)" >/dev/null
mkdir -p dist/GuruminVR
loader=$(find build/openxr-loader -name '*openxr_loader*.dll' | head -n1)
cp "$loader" dist/GuruminVR/openxr_loader.dll
x86_64-w64-mingw32-strip dist/GuruminVR/openxr_loader.dll
echo dist/GuruminVR/openxr_loader.dll
