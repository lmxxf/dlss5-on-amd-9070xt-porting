#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../../.."
case "${1:-fragment}" in
 motion) python3 Development/HIP/experiments/vit-select/prepare_motion.py; output=/tmp/benchmark_vit_motion.exe ;;
 fragment) python3 Development/HIP/experiments/vit-select/prepare_packed.py --fragments; output=/tmp/benchmark_vit_packed.exe ;;
 packed) python3 Development/HIP/experiments/vit-select/prepare_packed.py; output=/tmp/benchmark_vit_packed.exe ;;
 *) echo 'Use packed, fragment or motion' >&2; exit 2 ;;
esac
x86_64-w64-mingw32-g++ -std=c++17 -O2 -static -municode -D_WIN32_WINNT=0x0A00 -DNATIVE_GAME_TILED_VERIFICATION -DDLSS5_USE_HIP -I/tmp/vit-select-src/src /tmp/vit-select-src/Development/HIP/benchmark_live_capture.cpp -o "$output" -ld3d12 -ldxgi -ld3dcompiler -ldxguid
