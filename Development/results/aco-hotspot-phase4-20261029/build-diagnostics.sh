#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
p=Development/results/aco-hotspot-phase4-20261029
mkdir -p "$p/work" "$p/payload"
python3 "$p/prepare-frame.py"
x86_64-w64-mingw32-g++-posix -w -std=c++17 -O2 -static -municode -D_WIN32_WINNT=0x0A00 -DNATIVE_GAME_TILED_VERIFICATION -DDLSS5_USE_HIP -DHIP_SWIN_PERSISTENT=1 -DDLSS5_BENCH_BRIDGE_ISOLATE -Isrc -IDevelopment/HIP -I"$p/work/frame" "$p/work/frame/frame.cpp" -o "$p/payload/frame-timed.exe" -Wl,--image-base=0x140000000 -Wl,--no-insert-timestamp -ld3d12 -ldxgi -ld3dcompiler -ldxguid
python3 "$p/prepare-pure.py"
x86_64-w64-mingw32-g++-posix -w -std=c++17 -O2 -static -D_WIN32_WINNT=0x0A00 -IDevelopment/HIP -Isrc -IDevelopment/results/gap-phase6-20261023/knife1 "$p/pure.cpp" -o "$p/payload/pure.exe" -Wl,--image-base=0x140000000 -Wl,--no-insert-timestamp -ld3d12 -ldxgi -ld3dcompiler -ldxguid
llvm=/home/lmxxf/work/llvm-build-23/bin
"$llvm/clang" --target=x86_64-pc-windows-msvc -x hip --offload-device-only --offload-arch=gfx1201 -nogpuinc -nogpulib -O3 -c -emit-llvm "$p/stamp.hip" -o "$p/work/stamp.bc"
"$llvm/clang" -target amdgcn-amd-amdhsa -mcpu=gfx1201 -O3 -nogpulib -c "$p/work/stamp.bc" -o "$p/work/stamp.o"
"$llvm/ld.lld" -shared --no-undefined "$p/work/stamp.o" -o "$p/payload/stamp.hsaco"
python3 "$p/prepare-profile-device.py"
x86_64-w64-mingw32-g++-posix -w -std=c++17 -O2 -static -D_WIN32_WINNT=0x0A00 -I"$p/work/profile" -IDevelopment/HIP -Isrc "$p/work/profile/profile.cpp" -o "$p/payload/pure-profile-device-final.exe" -Wl,--image-base=0x140000000 -Wl,--no-insert-timestamp -ld3d12 -ldxgi -ld3dcompiler -ldxguid
