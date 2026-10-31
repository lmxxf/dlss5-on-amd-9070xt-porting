#!/usr/bin/env bash
set -euo pipefail
p=Development/results/yami-round5-20261031
r=$p/c512-scope/source
for mode in standard window; do
 extra=()
 if [[ $mode == window ]]; then extra=(-DBENCH_WIDTH=854 -DBENCH_HEIGHT=480); fi
 x86_64-w64-mingw32-g++-posix -w -std=c++17 -O2 -static -municode -D_WIN32_WINNT=0x0A00 -DNATIVE_GAME_TILED_VERIFICATION -DDLSS5_USE_HIP -DHIP_SWIN_PERSISTENT=1 "${extra[@]}" -I"$r/src" -I"$r/Development/HIP" "$r/Development/HIP/benchmark_live_capture.cpp" -o "$p/c512-scope-$mode.exe" -Wl,--image-base=0x140000000 -Wl,--no-insert-timestamp -ld3d12 -ldxgi -ld3dcompiler -ldxguid > "$p/c512-scope-$mode-build.log" 2>&1
done
