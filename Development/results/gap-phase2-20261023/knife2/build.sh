#!/usr/bin/env bash
set -euo pipefail
repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../../.." && pwd)
case_name=${1:-mix}
case "$case_name" in mix|mix-alt) ;; *) exit 2 ;; esac
work=/home/lmxxf/work/dlss5-gap-phase2-20261023/knife2
python3 "$repo/Development/tools/llvm-fork/compile-modules.py" \
 --hip "$work/$case_name/hip" --bin /home/lmxxf/work/llvm-build-23/bin \
 --out "$work/$case_name/build" --targets gfx1201 \
 --only c32-wave1-fast c32-wave1-fast-norm900 --compiler-rows llvm23 \
 --row-opts --target-feature=-real-true16 --jobs 2
