#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../../.."
out="$PWD/Development/results/backfeed-20261028/final-payload"
logs="$PWD/Development/results/backfeed-20261028/knife4"
bash scripts/build-addon.sh "$PWD/third_party/minhook" "$PWD/third_party/reshade/include" "$out/dlss5-amd.addon64" --hip > "$logs/addon.log" 2>&1
bash scripts/build-runtime.sh "$out" > "$logs/runtime.log" 2>&1
x86_64-w64-mingw32-g++-posix -w -std=c++17 -O2 -static -municode -D_WIN32_WINNT=0x0A00 -DNATIVE_GAME_TILED_VERIFICATION -DDLSS5_USE_HIP -DHIP_SWIN_PERSISTENT=1 -Isrc -IDevelopment/HIP Development/HIP/benchmark_live_capture.cpp -o "$out/frame.exe" -Wl,--image-base=0x140000000 -Wl,--no-insert-timestamp -ld3d12 -ldxgi -ld3dcompiler -ldxguid > "$logs/frame.log" 2>&1
sha256sum "$out"/*.exe "$out"/*.dll "$out"/*.addon64
