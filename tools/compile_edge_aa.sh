#!/bin/sh
# Compile only the mod-owned shader in an explicit Wine/Proton prefix.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
prefix=${GURUMIN_WINEPREFIX:?set GURUMIN_WINEPREFIX to the isolated Gurumin Proton prefix}
tool="$root/tools/edge_aa_compile.exe"
trap 'rm -f "$tool"' EXIT HUP INT TERM
i686-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -static "$root/tools/edge_aa_compile.cpp" \
  -o "$tool"
input=$(WINEPREFIX="$prefix" winepath -w "$root/src/edge_aa.hlsl")
output=$(WINEPREFIX="$prefix" winepath -w "$root/src/edge_aa_bytecode.hpp")
WINEPREFIX="$prefix" wine "$tool" "$input" "$output"
