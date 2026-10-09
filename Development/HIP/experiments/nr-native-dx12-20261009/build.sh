#!/usr/bin/env bash
set -euo pipefail
nr_dx12_root=$(cd "$(dirname "$0")/../../../.." && pwd)
nr_dx12_out=${1:-/tmp/nr-dx12-pure-build}
mkdir -p "$nr_dx12_out"
cd "$nr_dx12_root"
${NR_DX12_CXX:-x86_64-w64-mingw32-g++} -std=c++17 -O2 -static -w -municode -Isrc -IDevelopment/HIP Development/HIP/experiments/nr-native-dx12-20261009/pure.cpp -o "$nr_dx12_out/pure.exe" -ld3d12 -ldxgi -ld3dcompiler -ldxguid
sha256sum "$nr_dx12_out/pure.exe"
