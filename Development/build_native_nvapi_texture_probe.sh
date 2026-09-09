#!/usr/bin/env bash
set -euo pipefail
probe_source_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
if [[ $# -ne 1 || ! -f "$1/include/MinHook.h" ]]; then
  echo "usage: bash $0 /absolute/path/to/minhook/source" >&2
  exit 2
fi
probe_minhook_dir=$1
probe_build_dir=$(mktemp -d /tmp/native-texture-probe.XXXXXX)
probe_objects=()
for probe_unit in hook trampoline buffer hde/hde64; do
  probe_object="$probe_build_dir/${probe_unit##*/}.o"
  x86_64-w64-mingw32-gcc -O2 -I"$probe_minhook_dir/include" -I"$probe_minhook_dir/src" -c "$probe_minhook_dir/src/$probe_unit.c" -o "$probe_object"
  probe_objects+=("$probe_object")
done
mkdir -p "$probe_source_dir/release/nvidia-texture-probe"
probe_output="$probe_source_dir/release/nvidia-texture-probe/native-nvapi-texture-contract.addon64"
x86_64-w64-mingw32-g++ -std=c++17 -O2 -shared -static -I"$probe_minhook_dir/include" "$probe_source_dir/native_nvapi_texture_contract.cpp" "${probe_objects[@]}" -o "$probe_output"
sha256sum "$probe_output"
