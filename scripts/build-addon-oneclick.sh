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
out=${1:-native-game.addon64}
bash "$here/build-addon.sh" "$tp/minhook" "$tp/reshade/include" "$out" --tiled
echo "add-on: $out ($(stat -c %s "$out") bytes, sha256 $(sha256sum "$out" | cut -c1-16)…)"
