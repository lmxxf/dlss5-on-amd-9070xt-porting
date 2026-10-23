#!/usr/bin/env bash
set -euo pipefail
pair_src=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
pair_repo=$(cd "$pair_src/../../../../.." && pwd)
pair_out=/home/lmxxf/work/dlss5-gap-phase4-20261023/knife1
x86_64-w64-mingw32-g++ -std=c++17 -O2 -static -D_WIN32_WINNT=0x0A00 -I"$pair_repo/Development/HIP" "$pair_src/probe-loader.cpp" -o "$pair_out/pair-probe-loader.exe" -Wl,--image-base=0x140000000 -Wl,--no-insert-timestamp
sha256sum "$pair_out/pair-probe-loader.exe"
