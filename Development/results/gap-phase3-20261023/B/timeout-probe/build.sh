#!/usr/bin/env bash
set -euo pipefail
probe_src=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
probe_repo=$(cd "$probe_src/../../../../.." && pwd)
probe_out=/home/lmxxf/work/dlss5-gap-phase3-20261023/B
x86_64-w64-mingw32-g++ -std=c++17 -O2 -static -D_WIN32_WINNT=0x0A00 -I"$probe_repo/Development/HIP" "$probe_src/probe.cpp" -o "$probe_out/timeout-probe.exe" -Wl,--image-base=0x140000000 -Wl,--no-insert-timestamp
sha256sum "$probe_out/timeout-probe.exe"
