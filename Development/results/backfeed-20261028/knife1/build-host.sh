#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../../.."
out=Development/results/backfeed-20261028/knife1
geometry=${1:-standard}
args=()
if [[ "$geometry" == smallwindow ]]; then args=(-DBENCH_WIDTH=854 -DBENCH_HEIGHT=480); fi
x86_64-w64-mingw32-g++-posix -w -std=c++17 -O2 -static -municode -D_WIN32_WINNT=0x0A00 -DNATIVE_GAME_TILED_VERIFICATION -DDLSS5_USE_HIP -DHIP_SWIN_PERSISTENT=1 "${args[@]}" -Isrc -IDevelopment/HIP Development/HIP/benchmark_live_capture.cpp -o "$out/frame-$geometry.exe" -Wl,--image-base=0x140000000 -Wl,--no-insert-timestamp -ld3d12 -ldxgi -ld3dcompiler -ldxguid
sha256sum "$out/frame-$geometry.exe"
