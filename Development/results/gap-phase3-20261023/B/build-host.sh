#!/usr/bin/env bash
set -euo pipefail
work=/home/lmxxf/work/dlss5-gap-phase3-20261023/B
cd "$work/source"
x86_64-w64-mingw32-g++-posix -w -std=c++17 -O2 -static -municode -D_WIN32_WINNT=0x0A00 -DNATIVE_GAME_TILED_VERIFICATION -DDLSS5_USE_HIP -DDLSS5_C512_CHAIN_LAB -DHIP_SWIN_PERSISTENT=1 -I src -I Development/HIP Development/HIP/benchmark_live_capture.cpp -o "$work/frame.exe" -ld3d12 -ldxgi -ld3dcompiler -ldxguid
