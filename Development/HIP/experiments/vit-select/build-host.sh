#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../../.."
python3 Development/HIP/experiments/vit-select/prepare.py
x86_64-w64-mingw32-g++ -std=c++17 -O2 -static -municode -D_WIN32_WINNT=0x0A00 -DNATIVE_GAME_TILED_VERIFICATION -DDLSS5_USE_HIP -I/tmp/vit-select-src/src /tmp/vit-select-src/Development/HIP/benchmark_live_capture.cpp -o /tmp/benchmark_vit_select.exe -ld3d12 -ldxgi -ld3dcompiler -ldxguid
