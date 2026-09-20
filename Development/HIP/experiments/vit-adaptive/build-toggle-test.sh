#!/usr/bin/env bash
set -euo pipefail
python3 - <<'PY'
from pathlib import Path
p=Path('/tmp/vit-adaptive-src/Development/HIP/benchmark_live_capture.cpp');s=p.read_text();needle='for(UINT i=0;i<N;i++){';assert s.count(needle)==1
s=s.replace(needle,needle+'if(i==4)env("DLSS5_VIT_ADAPTIVE","0");if(i==8)env("DLSS5_VIT_ADAPTIVE","1");',1)
p.with_name('benchmark_toggle_test.cpp').write_text(s)
PY
x86_64-w64-mingw32-g++ -std=c++17 -O2 -static -municode -D_WIN32_WINNT=0x0A00 -DNATIVE_GAME_TILED_VERIFICATION -DDLSS5_USE_HIP -I/tmp/vit-adaptive-src/src /tmp/vit-adaptive-src/Development/HIP/benchmark_toggle_test.cpp -o /tmp/benchmark_vit_toggle.exe -ld3d12 -ldxgi -ld3dcompiler -ldxguid
