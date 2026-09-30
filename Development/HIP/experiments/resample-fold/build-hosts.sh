#!/usr/bin/env bash
# results/resample-fold-20261001: base = origin/main host before the fold (git stash of hip_reference_network.h not needed:
# pass BASE_REV), P = working tree host (W2_DOWN_FUSED handoff; falls back to Down() when the module lacks *_down),
# Proll = P + SP diagnostics (rollover/timeout runs). Run from repo root; $1 = output dir, $2 = base revision.
set -euo pipefail; out=${1:?}; rev=${2:?}; tmp=$(mktemp -d)
git archive "$rev" src Development/HIP | tar -x -C "$tmp"
cc(){ x86_64-w64-mingw32-g++ -w -std=c++17 -O2 -static -municode -D_WIN32_WINNT=0x0A00 -DHIP_SWIN_PERSISTENT=1 "${@:3}" -I "$1/src" -I "$1/Development/HIP" "$1/Development/HIP/benchmark_vit_reuse.cpp" -o "$out/benchmark-$2.exe" -ld3d12 -ldxgi -ld3dcompiler -ldxguid; }
cc "$tmp" base; cc . P; cc . Proll -DHIP_SWIN_PERSISTENT_DIAGNOSTICS=1
rm -rf "$tmp"; sha256sum "$out"/benchmark-*.exe
