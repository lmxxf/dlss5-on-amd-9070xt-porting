#!/usr/bin/env bash
set -euo pipefail
bench_src=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
bench_out=/home/lmxxf/work/dlss5-gap-refresh-20261023
python3 "$bench_src/generate_variants.py"
for variant in trace candidate candidate-trace; do
  unit=trace.cpp
  [[ "$variant" != candidate ]] || unit=benchmark.cpp
  x86_64-w64-mingw32-g++ -w -std=c++17 -O2 -static -D_WIN32_WINNT=0x0A00 -I"$bench_out/$variant-source/Development/HIP" -I"$bench_out/$variant-source/src" -I"$bench_src" "$bench_src/$unit" -o "$bench_out/$variant.exe" -Wl,--image-base=0x140000000 -Wl,--no-insert-timestamp -ld3d12 -ldxgi -ld3dcompiler -ldxguid
  sha256sum "$bench_out/$variant.exe"
done
