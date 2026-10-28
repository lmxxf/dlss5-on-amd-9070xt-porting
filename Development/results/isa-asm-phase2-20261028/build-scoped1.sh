#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
p=Development/results/isa-asm-phase2-20261028
for geometry in standard window; do
 args=();[[ $geometry == window ]] && args=(-DBENCH_WIDTH=854 -DBENCH_HEIGHT=480)
 x86_64-w64-mingw32-g++-posix -w -std=c++17 -O2 -static -municode -D_WIN32_WINNT=0x0A00 -DNATIVE_GAME_TILED_VERIFICATION -DDLSS5_USE_HIP -DHIP_SWIN_PERSISTENT=1 "${args[@]}" -I"$p/work/knife1-scoped-source/src" -I"$p/work/knife1-scoped-source/Development/HIP" Development/HIP/benchmark_live_capture.cpp -o "$p/work/frame-scoped-$geometry.exe" -Wl,--image-base=0x140000000 -Wl,--no-insert-timestamp -ld3d12 -ldxgi -ld3dcompiler -ldxguid
 done
