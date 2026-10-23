#!/usr/bin/env bash
set -euo pipefail
knife_src=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
knife_out=/home/lmxxf/work/dlss5-gap-phase2-20261023/knife1
root="$knife_out/final-source"
x86_64-w64-mingw32-g++ -w -std=c++17 -O2 -static -D_WIN32_WINNT=0x0A00 -I"$root/Development/HIP" -I"$root/src" -I"$knife_src" "$knife_src/pure.cpp" -o "$knife_out/final-pure.exe" -Wl,--image-base=0x140000000 -Wl,--no-insert-timestamp -ld3d12 -ldxgi -ld3dcompiler -ldxguid
x86_64-w64-mingw32-g++-posix -w -std=c++17 -O2 -static -municode -D_WIN32_WINNT=0x0A00 -DNATIVE_GAME_TILED_VERIFICATION -DDLSS5_USE_HIP -DHIP_SWIN_PERSISTENT=1 -I"$root/src" -I"$root/Development/HIP" "$root/Development/HIP/benchmark_live_capture.cpp" -o "$knife_out/final-frame.exe" -Wl,--image-base=0x140000000 -Wl,--no-insert-timestamp -ld3d12 -ldxgi -ld3dcompiler -ldxguid
root="$knife_out/final-trace-source"
x86_64-w64-mingw32-g++ -w -std=c++17 -O2 -static -D_WIN32_WINNT=0x0A00 -I"$root/Development/HIP" -I"$root/src" -I"$knife_src" "$knife_src/trace.cpp" -o "$knife_out/final-trace.exe" -Wl,--image-base=0x140000000 -Wl,--no-insert-timestamp -ld3d12 -ldxgi -ld3dcompiler -ldxguid
sha256sum "$knife_out"/final-*.exe
