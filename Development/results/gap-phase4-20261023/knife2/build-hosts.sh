#!/usr/bin/env bash
set -euo pipefail
work=/home/lmxxf/work/dlss5-gap-phase4-20261023/knife2
for role in baseline candidate; do
 root="$work/$role-source"
 for geometry in standard free1072; do
  args=()
  if [[ "$geometry" == free1072 ]]; then args=(-DBENCH_WIDTH=1920 -DBENCH_HEIGHT=1072); fi
  x86_64-w64-mingw32-g++-posix -w -std=c++17 -O2 -static -municode -D_WIN32_WINNT=0x0A00 -DNATIVE_GAME_TILED_VERIFICATION -DDLSS5_USE_HIP -DHIP_SWIN_PERSISTENT=1 "${args[@]}" -I"$root/src" -I"$root/Development/HIP" "$root/Development/HIP/benchmark_live_capture.cpp" -o "$work/frame-$role-$geometry.exe" -Wl,--image-base=0x140000000 -Wl,--no-insert-timestamp -ld3d12 -ldxgi -ld3dcompiler -ldxguid
 done
done
sha256sum "$work"/frame-*.exe
