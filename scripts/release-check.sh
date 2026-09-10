#!/usr/bin/env bash
# Pre-tag check: the repository must reproduce what the game is running. Run on the Linux side with ssh access to the AMD box.
#   1. shaders: compile every shader from shaders/ with compile-shaders.ps1 on the AMD box and compare each .cso hash with the
#      copy in the game's asset folder (D:\DLSSNR-Lab\native-game-tiled-assets) — catches sources missing from the repo,
#      hand-compiled kernels and stale compile lines.
#   2. flags: scripts/game-flags.txt vs the flag file the game reads (D:\DLSSNR-Lab\native-game-flags.txt), development-only
#      switches ignored — catches a public flag set left behind.
#   3. add-on: cross-build with build-addon-oneclick.sh and compare the hash with the installed add-on — catches an unpushed
#      host change (the compiler variant changes the hash, so this one is informational when it differs).
# usage: bash scripts/release-check.sh [amd-host]       exit 0 = all three green
set -uo pipefail
host=${1:-amd9070};here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd);root=$(cd -- "$here/.." && pwd);fail=0
lab='D:\DLSSNR-Lab';work="$lab\\logs\\release-check";assets="$lab\\native-game-tiled-assets"
game='C:\Program Files (x86)\Steam\steamapps\common\StellarBlade\SB\Binaries\Win64\native-submission-order.addon64'
tmp=$(mktemp -d)
echo "== 1. shaders (repo -> $host, compile only)"
tar czf "$tmp/repo.tgz" -C "$root" shaders scripts
scp -q "$tmp/repo.tgz" "$host:D:/DLSSNR-Lab/logs/release-check.tgz"
ssh "$host" "cd /d $lab\\logs && (rmdir /s /q release-check release-check-cso 2>nul) & mkdir release-check && cd release-check && tar xzf ..\\release-check.tgz && powershell -ExecutionPolicy Bypass -File scripts\\compile-shaders.ps1 -Folder $work-cso -DxcRoot $lab\\matrix-probe\\dxc-preview" >"$tmp/compile.log" 2>&1 || { echo "shader compile FAILED"; tail -5 "$tmp/compile.log"; fail=1; }
ssh "$host" "powershell -Command \"Get-ChildItem '$work-cso' -Filter *.cso | % { \$a=Join-Path '$assets' \$_.Name; if(Test-Path \$a){ if((Get-FileHash \$_.FullName).Hash -ne (Get-FileHash \$a).Hash){ 'DIFF ' + \$_.Name } } else { 'NEW ' + \$_.Name } }\"" | tr -d '\r' >"$tmp/cso.txt"
n=$(grep -c . "$tmp/cso.txt" || true);total=$(grep -c "^compiled" "$tmp/compile.log" || true)
if [ "$n" = "0" ]; then echo "shaders: every compiled .cso matches the game assets"; else echo "shaders: $n differ from the game assets:"; cat "$tmp/cso.txt"; fail=1; fi
echo "== 2. flags"
ssh "$host" "type $lab\\native-game-flags.txt" | tr -d "\r" | sed "s/[[:space:]]*$//" | grep -v '^DLSS5_DEBUG_DUMPS\|^DLSS5_RESERVE_VRAM_MB\|^DLSS5_BLACK_PROBE\|^DLSS5_GAME_PROBE\|^$' | sort >"$tmp/game.txt"
sed "s/[[:space:]]*$//" "$root/scripts/game-flags.txt" | grep -v '^DLSS5_DEBUG_DUMPS\|^DLSS5_RESERVE_VRAM_MB\|^DLSS5_BLACK_PROBE\|^DLSS5_GAME_PROBE\|^$' | sort >"$tmp/repo.txt"
if diff -q "$tmp/repo.txt" "$tmp/game.txt" >/dev/null; then echo "flags: scripts/game-flags.txt == the flag file the game reads"; else echo "flags: DIFFER (< repo, > game):"; diff "$tmp/repo.txt" "$tmp/game.txt"; fail=1; fi
echo "== 3. add-on"
bash "$here/build-addon-oneclick.sh" "$tmp/check.addon64" >"$tmp/addon.log" 2>&1 || { echo "add-on build FAILED"; tail -3 "$tmp/addon.log"; fail=1; }
local_hash=$(sha256sum "$tmp/check.addon64" 2>/dev/null | cut -c1-64);installed=$(ssh "$host" "powershell -Command \"(Get-FileHash '$game').Hash.ToLower()\"" | tr -d '\r')
if [ "$local_hash" = "$installed" ]; then echo "add-on: repo build == installed ($installed)"; else echo "add-on: repo build ${local_hash:0:12} != installed ${installed:0:12} (a host change not deployed, or a different compiler)"; fi
rm -rf "$tmp"
[ $fail = 0 ] && echo "RELEASE CHECK: all green" || echo "RELEASE CHECK: FAILED"
exit $fail
