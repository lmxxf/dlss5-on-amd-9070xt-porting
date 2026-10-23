#!/usr/bin/env bash
set -euo pipefail
own=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
work=/home/lmxxf/work/dlss5-gap-phase4-20261023/knife1
src="$work/source"
x86_64-w64-mingw32-g++ -w -std=c++17 -O2 -static -D_WIN32_WINNT=0x0A00 -I"$src/Development/HIP" -I"$src/src" -I"$own" "$own/pure.cpp" -o "$work/pure.exe" -Wl,--image-base=0x140000000 -Wl,--no-insert-timestamp -ld3d12 -ldxgi -ld3dcompiler -ldxguid
x86_64-w64-mingw32-g++-posix -w -std=c++17 -O2 -static -municode -D_WIN32_WINNT=0x0A00 -DNATIVE_GAME_TILED_VERIFICATION -DDLSS5_USE_HIP -DHIP_SWIN_PERSISTENT=1 -I"$src/src" -I"$src/Development/HIP" "$src/Development/HIP/benchmark_live_capture.cpp" -o "$work/frame.exe" -Wl,--image-base=0x140000000 -Wl,--no-insert-timestamp -ld3d12 -ldxgi -ld3dcompiler -ldxguid
sha256sum "$work/pure.exe" "$work/frame.exe"
