#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
p=Development/results/c32-stage-phase5b-20261029
role=${1:-3};src="$p/work/$role-host"
for mode in standard window;do
 extra=();name="$role-frame.exe";if [[ "$mode" == window ]];then extra=(-DBENCH_WIDTH=854 -DBENCH_HEIGHT=480);name="$role-frame-window.exe";fi
 x86_64-w64-mingw32-g++-posix -w -std=c++17 -O2 -static -municode -D_WIN32_WINNT=0x0A00 -DNATIVE_GAME_TILED_VERIFICATION -DDLSS5_USE_HIP -DHIP_SWIN_PERSISTENT=1 "${extra[@]}" -I"$src/src" -I"$src/Development/HIP" "$src/frame.cpp" -o "$p/payload/$name" -Wl,--image-base=0x140000000 -Wl,--no-insert-timestamp -ld3d12 -ldxgi -ld3dcompiler -ldxguid > "$p/build-$role-$mode.log" 2>&1
done
P5_SOURCE_ROOT="$PWD/$src" python3 "$p/prepare-profile-device.py"
x86_64-w64-mingw32-g++-posix -w -std=c++17 -O2 -static -D_WIN32_WINNT=0x0A00 -I"$p/work/profile" -IDevelopment/HIP -Isrc "$p/work/profile/profile.cpp" -o "$p/payload/$role-profile.exe" -Wl,--image-base=0x140000000 -Wl,--no-insert-timestamp -ld3d12 -ldxgi -ld3dcompiler -ldxguid > "$p/build-$role-profile.log" 2>&1
