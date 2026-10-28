#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../../.."
root=Development/results/backfeed-20261028/knife3
for role in baseline candidate; do
 python3 Development/tools/llvm-fork/compile-modules.py --hip "$root/$role-source/hip" --bin /home/lmxxf/work/llvm-build-23/bin --out "$root/$role" --targets gfx1201 --only c64-wave2 c64-wave2-fast --compiler-rows llvm23 --row-opts --target-feature=-real-true16 --jobs 1
 done
