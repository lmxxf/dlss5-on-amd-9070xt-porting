#!/usr/bin/env bash
set -euo pipefail
repo=/home/lmxxf/work/ai-theorys-study/wechat/assets/297
out="$repo/Development/results/aco-hotspot-phase4-20261029/payload"
cd /home/lmxxf/work/dlss5-phase4-mochi-build
source windows/build/arch/rdna4.sh
python3 - <<'PY'
import re
from pathlib import Path
s=Path('/usr/include/vulkan/vulkan_core.h').read_text();names=sorted(set(re.findall(r'VKAPI_CALL\s+(vk\w+)\s*\(',s)));Path('vulkan-phase4.def').write_text('LIBRARY vulkan-1.dll\nEXPORTS\n'+'\n'.join(names)+'\n')
PY
x86_64-w64-mingw32-dlltool -d vulkan-phase4.def -l vulkan-phase4.a
x86_64-w64-mingw32-g++-posix -O2 -std=c++17 -static -D_WIN32_WINNT=0x0A00 "${NR_PRODUCT_DEFINES[@]}" -Iphase4-include -Iwindows/src/core windows/src/core/nr_graph.cpp vulkan-phase4.a -o "$out/mochi-native.exe" -Wl,--no-insert-timestamp
