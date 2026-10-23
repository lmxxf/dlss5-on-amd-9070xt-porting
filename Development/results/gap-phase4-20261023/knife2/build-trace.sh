#!/usr/bin/env bash
set -euo pipefail
own=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
work=/home/lmxxf/work/dlss5-gap-phase4-20261023/knife2
for role in baseline candidate; do
 root="$work/$role-trace-source"
 x86_64-w64-mingw32-g++ -w -std=c++17 -O2 -static -D_WIN32_WINNT=0x0A00 -I"$root/Development/HIP" -I"$root/src" -I"$own" "$own/trace.cpp" -o "$work/trace-$role.exe" -Wl,--image-base=0x140000000 -Wl,--no-insert-timestamp -ld3d12 -ldxgi -ld3dcompiler -ldxguid
 done
sha256sum "$work"/trace-*.exe
