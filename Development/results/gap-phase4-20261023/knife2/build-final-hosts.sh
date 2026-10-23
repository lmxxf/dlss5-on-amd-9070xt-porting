#!/usr/bin/env bash
set -euo pipefail
own=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo=$(cd -- "$own/../../../.." && pwd)
work=/home/lmxxf/work/dlss5-gap-phase4-20261023/knife2/final-host
mkdir -p "$work"
for geometry in standard free1072; do
 args=()
 if [[ "$geometry" == free1072 ]]; then args=(-DBENCH_WIDTH=1920 -DBENCH_HEIGHT=1072); fi
 x86_64-w64-mingw32-g++-posix -w -std=c++17 -O2 -static -municode -D_WIN32_WINNT=0x0A00 -DNATIVE_GAME_TILED_VERIFICATION -DDLSS5_USE_HIP -DHIP_SWIN_PERSISTENT=1 "${args[@]}" -I"$repo/src" -I"$repo/Development/HIP" "$repo/Development/HIP/benchmark_live_capture.cpp" -o "$work/final-frame-$geometry.exe" -Wl,--image-base=0x140000000 -Wl,--no-insert-timestamp -ld3d12 -ldxgi -ld3dcompiler -ldxguid
 done
pure_src="$repo/Development/results/gap-phase4-20261023/knife1"
x86_64-w64-mingw32-g++ -w -std=c++17 -O2 -static -D_WIN32_WINNT=0x0A00 -I"$repo/Development/HIP" -I"$repo/src" -I"$pure_src" "$pure_src/pure.cpp" -o "$work/final-pure.exe" -Wl,--image-base=0x140000000 -Wl,--no-insert-timestamp -ld3d12 -ldxgi -ld3dcompiler -ldxguid
sha256sum "$work"/*.exe
