#!/usr/bin/env bash
set -euo pipefail
probe_src=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo=$(cd "$probe_src/../../../../../.." && pwd)
work=/home/lmxxf/work/dlss5-gap-phase6-20261023/knife1
x86_64-w64-mingw32-g++ -std=c++17 -O2 -static -D_WIN32_WINNT=0x0A00 -I"$repo/Development/HIP" "$probe_src/probe.cpp" -o "$work/pair-probe.exe" -Wl,--image-base=0x140000000 -Wl,--no-insert-timestamp
sha256sum "$work/pair-probe.exe"
