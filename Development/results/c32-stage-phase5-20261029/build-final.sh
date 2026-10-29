#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
p=Development/results/c32-stage-phase5-20261029
out="$PWD/$p/payload";logs="$PWD/$p/final-build";mkdir -p "$logs"
bash scripts/build-addon.sh "$PWD/third_party/minhook" "$PWD/third_party/reshade/include" "$out/dlss5-amd.addon64" --hip > "$logs/addon.log" 2>&1
bash scripts/build-runtime.sh "$out" > "$logs/runtime.log" 2>&1
for mode in standard window;do
 extra=();if [[ "$mode" == window ]];then extra=(-DBENCH_WIDTH=854 -DBENCH_HEIGHT=480);fi
 x86_64-w64-mingw32-g++-posix -w -std=c++17 -O2 -static -municode -D_WIN32_WINNT=0x0A00 -DNATIVE_GAME_TILED_VERIFICATION -DDLSS5_USE_HIP -DHIP_SWIN_PERSISTENT=1 "${extra[@]}" -Isrc -IDevelopment/HIP Development/HIP/benchmark_live_capture.cpp -o "$out/frame-$mode.exe" -Wl,--image-base=0x140000000 -Wl,--no-insert-timestamp -ld3d12 -ldxgi -ld3dcompiler -ldxguid > "$logs/frame-$mode.log" 2>&1
done
x86_64-w64-mingw32-g++-posix -w -std=c++17 -O2 -static -D_WIN32_WINNT=0x0A00 -IDevelopment/HIP -Isrc -IDevelopment/results/gap-phase6-20261023/knife1 "$p/pure.cpp" -o "$out/pure.exe" -Wl,--image-base=0x140000000 -Wl,--no-insert-timestamp -ld3d12 -ldxgi -ld3dcompiler -ldxguid > "$logs/pure.log" 2>&1
sha256sum "$out/frame-standard.exe" "$out/frame-window.exe" "$out/pure.exe" "$out/LmxxfNrRuntime.dll" "$out/dlss5-amd.addon64" > "$logs/hosts.sha256"

python3 - <<'PYSOURCE'
from pathlib import Path
import json,hashlib
p=Path('Development/results/c32-stage-phase5-20261029');(p/'final-build/source-lock.json').write_text(json.dumps({str(f):hashlib.sha256(f.read_bytes()).hexdigest()for f in [Path('Development/HIP/hip_reference_network.h'),Path('Development/HIP/benchmark_live_capture.cpp'),Path('src/LmxxfNrRuntime.cpp'),p/'pure.cpp']},indent=2))
PYSOURCE
