#!/usr/bin/env bash
set -euo pipefail
pipeline_start_ns=$(date +%s%N)
frame=${1:?usage: run_dynamic_frame_pipeline.sh FRAME [SSH_HOST]}
host=${2:-amd9070}
root=$(cd "$(dirname "$0")" && pwd)
work="/tmp/dlss5-raw-$frame"
mkdir -p "$work"
R='D:\DLSSNR-Lab'
remote(){ ssh "$host" "$@"; }
pull(){ scp "$host:D:/DLSSNR-Lab/$1" "$2"; }
push(){ scp "$1" "$host:D:/DLSSNR-Lab/$2"; }

pull "logs/ffx-color-$frame.bin" "$work/color.bin"
pull "logs/dynamic-frame-$frame.bin" "$work/backbuffer.bin"
sha256sum "$work/color.bin" "$work/backbuffer.bin"
python3 "$root/prepare_dynamic_preblock_input.py" "$work/color.bin" "$work/tiles.rgba32f" "$work/block0-zero.fp8"
push "$work/tiles.rgba32f" "raw-$frame-tiles.rgba32f"
push "$work/block0-zero.fp8" "raw-$frame-block0-zero.fp8"
remote "cmd /c $R\\d3d12_preblock_test.exe $R\\block0-distilled.bin $R\\raw-$frame-tiles.rgba32f $R\\raw-$frame-block0-zero.fp8 $R\\raw-$frame-block0.f32"
remote "cmd /c $R\\preblock_tiles_to_hwc.exe $R\\raw-$frame-block0.f32 $R\\tinlayout-2h64-output-permutation.i32 $R\\raw-$frame-block0-hwc.f32"

remote "cmd /c $R\\decode_r10g10b10a2.exe $R\\logs\\dynamic-frame-$frame.bin $R\\raw-$frame-backbuffer-rgba.f32"
remote "powershell.exe -NoProfile -ExecutionPolicy Bypass -File $R\\run_dynamic_network_resident.ps1 -Frame $frame"
pull "dlss5-output-r10.bin" "$work/output-r10.bin"
python3 - "$work/output-r10.bin" "$root/dynamic-captures/dynamic-frame-$frame-dlss5-synchronous.png" <<'PY'
import sys,numpy as np
from PIL import Image
v=np.fromfile(sys.argv[1],'<u4').reshape(2160,3840)
rgb=np.stack((v&1023,(v>>10)&1023,(v>>20)&1023),-1)
Image.fromarray(np.rint(rgb*(255/1023)).astype(np.uint8),'RGB').save(sys.argv[2])
PY
sha256sum "$work/output-r10.bin"
pipeline_end_ns=$(date +%s%N)
python3 - "$pipeline_start_ns" "$pipeline_end_ns" <<'PY'
import sys
print(f"pipeline_wall_seconds={(int(sys.argv[2])-int(sys.argv[1]))/1e9:.3f}")
PY
