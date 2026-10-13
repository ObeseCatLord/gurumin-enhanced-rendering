#!/bin/sh
set -eu
cd "$(dirname "$0")"
mkdir -p dist
for name in buffer hook trampoline; do
 i686-w64-mingw32-gcc -O2 -Ivendor/minhook/include -c vendor/minhook/src/$name.c -o dist/$name.o
done
i686-w64-mingw32-gcc -O2 -c vendor/minhook/src/hde/hde32.c -o dist/hde32.o
i686-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -static -shared -Ivendor/minhook/include src/display.cpp src/d3d9.def dist/buffer.o dist/hook.o dist/trampoline.o dist/hde32.o -o dist/d3d9.dll -Wl,--kill-at -ladvapi32

# x64 OpenXR host for VR mode (Windows and Proton) and the Khronos loader.
mkdir -p dist/GuruminVR
x86_64-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Wno-missing-field-initializers -municode -static -Ivendor/openxr/include host/gurumin_vr_host.cpp -o dist/GuruminVR/gurumin_vr_host.exe -ld3d11 -ldxgi
if [ ! -f dist/GuruminVR/openxr_loader.dll ] || [ "${REBUILD_OPENXR_LOADER-}" = 1 ]; then
 ./tools/build_openxr_loader.sh
fi
cp vendor/openxr/LICENSE dist/GuruminVR/OpenXR-LICENSE.txt

cp vendor/fxaa/LICENSE.txt dist/GuruminModern-FXAA-LICENSE.txt
cp packaging/GuruminModern.ini dist/GuruminModern.ini
