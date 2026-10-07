#!/bin/sh
set -eu
cd "$(dirname "$0")"
mkdir -p dist
for name in buffer hook trampoline; do
 i686-w64-mingw32-gcc -O2 -Ivendor/minhook/include -c vendor/minhook/src/$name.c -o dist/$name.o
done
i686-w64-mingw32-gcc -O2 -c vendor/minhook/src/hde/hde32.c -o dist/hde32.o
i686-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -static -shared -Ivendor/minhook/include src/display.cpp src/d3d9.def dist/buffer.o dist/hook.o dist/trampoline.o dist/hde32.o -o dist/d3d9.dll -Wl,--kill-at -ladvapi32

cp vendor/fxaa/LICENSE.txt dist/GuruminModern-FXAA-LICENSE.txt
cp packaging/GuruminModern.ini dist/GuruminModern.ini
