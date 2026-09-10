#!/usr/bin/env bash
# One-click cross build of the add-on on Linux / WSL (Ubuntu): fetches MinHook and the ReShade 6.8 add-on headers into
# third_party/ (git, network needed the first time) and runs build-addon.sh. Needs: sudo apt install g++-mingw-w64-x86-64 git
# Output: native-game.addon64 in the current directory (copy it next to d3d12.dll in the game as dlss5-amd.addon64 or deploy
# it with scripts/deploy_fast.ps1 on the Windows machine).
set -euo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
root=$(cd -- "$here/.." && pwd)
tp="$root/third_party"
mkdir -p "$tp"
command -v x86_64-w64-mingw32-g++ >/dev/null || { echo "missing x86_64-w64-mingw32-g++: sudo apt install g++-mingw-w64-x86-64" >&2; exit 2; }
[ -f "$tp/minhook/include/MinHook.h" ] || git clone -q --depth 1 https://github.com/TsudaKageyu/minhook "$tp/minhook"
if [ ! -f "$tp/reshade/include/reshade.hpp" ]; then
  git clone -q --depth 1 --branch v6.8.0 --filter=blob:none --sparse https://github.com/crosire/reshade "$tp/reshade"
  (cd "$tp/reshade" && git sparse-checkout set include >/dev/null)
fi
# mingw-w64 < 10 ships a d3d12.h without ID3D12SDKConfiguration (needed to select the Agility SDK): fetch the v11 headers.
if ! grep -q ID3D12SDKConfiguration "$(x86_64-w64-mingw32-g++ -print-sysroot 2>/dev/null)/usr/share/mingw-w64/include/d3d12.h" /usr/share/mingw-w64/include/d3d12.h /usr/x86_64-w64-mingw32/include/d3d12.h 2>/dev/null; then
  mkdir -p "$tp/mingw-headers"
  for h in d3d12.h d3d12sdklayers.h d3dcommon.h dxgicommon.h dxgiformat.h dxgitype.h dxgi.h dxgi1_2.h dxgi1_3.h dxgi1_4.h dxgi1_5.h dxgi1_6.h; do
    [ -f "$tp/mingw-headers/$h" ] || curl -fsSL "https://raw.githubusercontent.com/mingw-w64/mingw-w64/v11.0.1/mingw-w64-headers/include/$h" -o "$tp/mingw-headers/$h"
  done
  export DLSS5_EXTRA_INCLUDE="$tp/mingw-headers"
  echo "using mingw-w64 v11 D3D12/DXGI headers from third_party/mingw-headers (system mingw-w64 is too old)"
fi
out=${1:-native-game.addon64}
bash "$here/build-addon.sh" "$tp/minhook" "$tp/reshade/include" "$out" --tiled
echo "add-on: $out ($(stat -c %s "$out") bytes, sha256 $(sha256sum "$out" | cut -c1-16)…)"
