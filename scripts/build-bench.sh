#!/usr/bin/env bash
# Cross-builds the bench executable with mingw-w64 (run on Linux; copy the .exe next to the shaders and weights).
set -euo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
out=${1:-native-network70-temporal.exe}
x86_64-w64-mingw32-g++ -w -DMATRIX_BENCH -std=c++17 -O2 -static -municode "$here/../src/d3d12_native_network70_test.cpp" -o "$out" -ld3d12 -ldxgi -ld3dcompiler -ldxguid
echo "built $out"
