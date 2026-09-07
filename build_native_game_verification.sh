#!/usr/bin/env bash
set -euo pipefail
if [[ $# -ne 3 || ! -f "$1/include/MinHook.h" || ! -f "$2/reshade.hpp" ]]; then
  echo "usage: bash $0 MINHOOK_SOURCE RESHADE_INCLUDE OUTPUT_ADDON64" >&2
  exit 2
fi
probe_source_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
probe_minhook_dir=$1
probe_reshade_include=$2
probe_output=$3
probe_build_dir=$(mktemp -d /tmp/native-game-verification.XXXXXX)
probe_objects=()
for probe_unit in hook trampoline buffer hde/hde64; do
  probe_object="$probe_build_dir/${probe_unit##*/}.o"
  x86_64-w64-mingw32-gcc -O2 -I"$probe_minhook_dir/include" -I"$probe_minhook_dir/src" -c "$probe_minhook_dir/src/$probe_unit.c" -o "$probe_object"
  probe_objects+=("$probe_object")
done
# ReShade's Windows SDK include spelling is case-sensitive on Linux.
ln -s /usr/x86_64-w64-mingw32/include/windows.h "$probe_build_dir/Windows.h"
x86_64-w64-mingw32-g++ -w -std=c++17 -O2 -shared -static -DNATIVE_ORDER_NEURAL \
  -I"$probe_build_dir" -I"$probe_minhook_dir/include" -I"$probe_reshade_include" \
  "$probe_source_dir/native_submission_order_probe.cpp" "${probe_objects[@]}" \
  -o "$probe_output" -ld3d12 -ldxgi -ld3dcompiler -ldxguid
sha256sum "$probe_output"
echo 'Diagnostic single-frame DLL built; not deployed or accepted as a temporal renderer.'
