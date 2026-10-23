#!/usr/bin/env bash
set -euo pipefail
bench_src=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
bench_root=$(cd "$bench_src/../../../.." && pwd)
bench_out=/home/lmxxf/work/dlss5-gap-refresh-20261023
mkdir -p "$bench_out"
x86_64-w64-mingw32-g++ -w -std=c++17 -O2 -static -D_WIN32_WINNT=0x0A00 -MMD -MF "$bench_out/benchmark.d" -I"$bench_src" -I"$bench_root/src" -I"$bench_root/Development/HIP" "$bench_src/benchmark.cpp" -o "$bench_out/benchmark.exe" -Wl,--image-base=0x140000000 -Wl,--no-insert-timestamp -ld3d12 -ldxgi -ld3dcompiler -ldxguid
sha256sum "$bench_out/benchmark.exe"
