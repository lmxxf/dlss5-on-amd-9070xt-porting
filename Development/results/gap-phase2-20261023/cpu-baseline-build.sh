#!/usr/bin/env bash
# CPU-only, unmodified LLVM23 production C32 baseline. Does not run or upload GPU code.
set -euo pipefail
repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../.." && pwd)
out=${1:-/home/lmxxf/work/dlss5-phase2-build-20261023/baseline-llvm23}
python3 "$repo/Development/tools/llvm-fork/compile-modules.py" \
  --hip "$repo/hip" --bin /home/lmxxf/work/llvm-build-23/bin \
  --out "$out" --targets gfx1201 --only c32-wave1-fast \
  --compiler-rows llvm23 --row-opts --target-feature=-real-true16 --jobs 1
python3 - "$repo" "$out" <<'PY'
import hashlib,pathlib,sys
repo,out=map(pathlib.Path,sys.argv[1:])
sums={p:h for h,p in (line.split() for line in (repo/'hip/SHA256SUMS').read_text().splitlines() if line.strip())}
p=out/'gfx1201/c32-wave1-fast.hsaco'
h=hashlib.sha256(p.read_bytes()).hexdigest()
assert h==sums['gfx1201/c32-wave1-fast.hsaco'], (h,sums['gfx1201/c32-wave1-fast.hsaco'])
print('BASELINE_WHOLE_HSACO_SHA_MATCH',h)
PY
