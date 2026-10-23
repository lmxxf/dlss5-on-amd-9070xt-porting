#!/usr/bin/env bash
set -euo pipefail
work=/home/lmxxf/work/dlss5-gap-phase6-20261023/knife2/up-route
root="$work/source"
for shape in standard free1072; do
 defs=()
 [[ "$shape" != free1072 ]] || defs=(-DBENCH_WIDTH=1920 -DBENCH_HEIGHT=1072)
 x86_64-w64-mingw32-g++-posix -w -std=c++17 -O2 -static -municode -D_WIN32_WINNT=0x0A00 -DNATIVE_GAME_TILED_VERIFICATION -DDLSS5_USE_HIP -DHIP_SWIN_PERSISTENT=1 "${defs[@]}" -I"$root/src" -I"$root/Development/HIP" "$root/Development/HIP/benchmark_live_capture.cpp" -o "$work/frame-$shape.exe" -Wl,--image-base=0x140000000 -Wl,--no-insert-timestamp -ld3d12 -ldxgi -ld3dcompiler -ldxguid
 sha256sum "$work/frame-$shape.exe"
done
