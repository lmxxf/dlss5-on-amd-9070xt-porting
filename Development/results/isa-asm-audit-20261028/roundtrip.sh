#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
root=Development/results/isa-asm-audit-20261028
python3 Development/tools/llvm-fork/compile-modules.py --hip hip --bin /home/lmxxf/work/llvm-build-23/bin --out "$root/work/roundtrip" --targets gfx1201 --only c32-wave1-fast --compiler-rows llvm23 --row-opts --target-feature=-real-true16 --jobs 1
/home/lmxxf/work/llvm-build-23/bin/clang -cc1as -triple amdgcn-amd-amdhsa -target-cpu gfx1201 -target-feature -real-true16 -filetype obj "$root/work/roundtrip/gfx1201/c32-wave1-fast.hsaco.s" -o "$root/work/roundtrip/reassembled.o"
/home/lmxxf/work/llvm-build-23/bin/ld.lld --no-undefined -shared -plugin-opt=mcpu=gfx1201 "$root/work/roundtrip/reassembled.o" -o "$root/work/roundtrip/reassembled.hsaco"
