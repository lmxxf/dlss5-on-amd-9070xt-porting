#!/usr/bin/env bash
set -euo pipefail
knife_src=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
knife_out=/home/lmxxf/work/dlss5-gap-phase2-20261023/knife1
for arm in baseline candidate; do
 root="$knife_out/$arm-trace-source"
 x86_64-w64-mingw32-g++ -w -std=c++17 -O2 -static -D_WIN32_WINNT=0x0A00 -I"$root/Development/HIP" -I"$root/src" -I"$knife_src" "$knife_src/trace.cpp" -o "$knife_out/trace-$arm.exe" -Wl,--image-base=0x140000000 -Wl,--no-insert-timestamp -ld3d12 -ldxgi -ld3dcompiler -ldxguid
 sha256sum "$knife_out/trace-$arm.exe"
done
