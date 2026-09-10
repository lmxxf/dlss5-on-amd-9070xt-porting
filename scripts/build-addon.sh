#!/usr/bin/env bash
set -euo pipefail
if [[ $# -lt 3 || $# -gt 4 || ! -f "$1/include/MinHook.h" || ! -f "$2/reshade.hpp" ]]; then
  echo "usage: bash $0 MINHOOK_SOURCE RESHADE_INCLUDE OUTPUT_ADDON64 [--tiled]" >&2
  exit 2
fi
probe_source_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
probe_minhook_dir=$1
probe_reshade_include=$2
probe_output=$3
probe_defines=()
if [[ $# -eq 4 ]]; then
  [[ $4 == --tiled ]] || exit 2
  probe_defines+=(-DNATIVE_GAME_TILED_VERIFICATION)
fi
probe_build_dir=$(mktemp -d /tmp/native-game-verification.XXXXXX)
probe_objects=()
for probe_unit in hook trampoline buffer hde/hde64; do
  probe_object="$probe_build_dir/${probe_unit##*/}.o"
  x86_64-w64-mingw32-gcc -O2 -I"$probe_minhook_dir/include" -I"$probe_minhook_dir/src" -c "$probe_minhook_dir/src/$probe_unit.c" -o "$probe_object"
  probe_objects+=("$probe_object")
done
# ReShade's Windows SDK include spelling is case-sensitive on Linux.
ln -s /usr/x86_64-w64-mingw32/include/windows.h "$probe_build_dir/Windows.h"
# Older mingw-w64 (Ubuntu 22.04: gcc 10, win32 thread model, mingw-w64 8): std::mutex needs the -posix variant of the compiler,
# GetTickCount64 / SRW locks need _WIN32_WINNT >= Vista, and d3d12.h lacks ID3D12SDKConfiguration (build-addon-oneclick.sh drops
# newer headers into $DLSS5_EXTRA_INCLUDE, searched first).
probe_cxx=x86_64-w64-mingw32-g++
probe_gcc_major=$("$probe_cxx" -dumpversion | sed 's/[^0-9].*//')
if [ "${probe_gcc_major:-0}" -lt 13 ] && "$probe_cxx" -v 2>&1 | grep -q "Thread model: win32"; then
  if command -v x86_64-w64-mingw32-g++-posix >/dev/null; then probe_cxx=x86_64-w64-mingw32-g++-posix
  else echo "gcc $probe_gcc_major with the win32 thread model has no std::mutex: sudo apt install g++-mingw-w64-x86-64-posix" >&2; exit 2; fi
fi
probe_extra_include=();[ -n "${DLSS5_EXTRA_INCLUDE:-}" ] && probe_extra_include=(-I"$DLSS5_EXTRA_INCLUDE")
"$probe_cxx" -w -std=c++17 -O2 -shared -static -D_WIN32_WINNT=0x0A00 -DNATIVE_ORDER_NEURAL "${probe_defines[@]}" \
  "${probe_extra_include[@]}" -I"$probe_build_dir" -I"$probe_minhook_dir/include" -I"$probe_reshade_include" \
  "$probe_source_dir/../src/native_submission_order_probe.cpp" "${probe_objects[@]}" \
  -o "$probe_output" -ld3d12 -ldxgi -ld3dcompiler -ldxguid
sha256sum "$probe_output"
echo 'Diagnostic single-frame DLL built; not deployed or accepted as a temporal renderer.'
