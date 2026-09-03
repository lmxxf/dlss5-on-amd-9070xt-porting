#!/usr/bin/env bash
set -euo pipefail
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
python3 "$root/prepare_dynamic_preblock_input.py" "$work/color.bin" "$work/tiles.rgba32f" "$work/block0-zero.fp8"
push "$work/tiles.rgba32f" "raw-$frame-tiles.rgba32f"
push "$work/block0-zero.fp8" "raw-$frame-block0-zero.fp8"
remote "cmd /c $R\\d3d12_preblock_test.exe $R\\block0-distilled.bin $R\\raw-$frame-tiles.rgba32f $R\\raw-$frame-block0-zero.fp8 $R\\raw-$frame-block0.f32"
remote "cmd /c $R\\preblock_tiles_to_hwc.exe $R\\raw-$frame-block0.f32 $R\\tinlayout-2h64-output-permutation.i32 $R\\raw-$frame-block0-hwc.f32"

remote "powershell.exe -NoProfile -ExecutionPolicy Bypass -File $R\\run_dynamic_encoder_raw13.ps1 -Frame $frame"
remote "powershell.exe -NoProfile -ExecutionPolicy Bypass -File $R\\run_dynamic_raw14_30.ps1 -Frame $frame"
remote "cmd /c $R\\pad_hwc_rows.exe $R\\raw-$frame-block30-34x60.f32 $R\\raw-$frame-block30.f32 34 36 60 1024"
remote "powershell.exe -NoProfile -ExecutionPolicy Bypass -File $R\\run_dynamic_vit_raw.ps1 -Frame $frame"

remote "cmd /c $R\\prepare_block39_dynamic_input.exe $R\\raw-$frame-block38.f32 $R\\raw-$frame-block30-body.f32 $R\\raw-$frame-block39-input.f32"
remote "cmd /c $R\\d3d12_affine_test.exe $R\\block39-logical-effective-bias.bin $R\\raw-$frame-block39-input.f32 $R\\raw-$frame-block39.f32 1536 512"
remote "powershell.exe -NoProfile -ExecutionPolicy Bypass -File $R\\run_dynamic_decoder_raw40_47.ps1 -Frame $frame"

remote "cmd /c $R\\d3d12_affine_test.exe $R\\block48-prefix-matrix-with-bias.bin $R\\raw-$frame-block47.f32 $R\\raw-$frame-block48-projected.f32 512 256"
remote "cmd /c $R\\merge_upsample_skip.exe $R\\raw-$frame-block48-projected.f32 $R\\raw-$frame-block22-body.f32 $R\\raw-$frame-block48-prefix.f32 68 120 256"
remote "cmd /c $R\\d3d12_block128_test.exe $R\\block48-body-effective.bin $R\\raw-$frame-block48-prefix.f32 $R\\raw-$frame-block48.f32 240 136 0"
remote "powershell.exe -NoProfile -ExecutionPolicy Bypass -File $R\\run_dynamic_decoder_raw49_55.ps1 -Frame $frame"

upsample(){
 local block=$1 prev=$2 skip=$3 cin=$4 cout=$5 h=$6 w=$7
 remote "cmd /c $R\\d3d12_affine_test.exe $R\\block$block-prefix-matrix-with-bias.bin $R\\raw-$frame-block$prev.f32 $R\\raw-$frame-block$block-projected.f32 $cin $cout"
 remote "cmd /c $R\\merge_upsample_skip.exe $R\\raw-$frame-block$block-projected.f32 $R\\raw-$frame-block$skip.f32 $R\\raw-$frame-block$block-prefix.f32 $h $w $cout"
}
upsample 56 55 14 256 128 136 240
remote "cmd /c $R\\d3d12_block128_test.exe $R\\block56-body-effective.bin $R\\raw-$frame-block56-prefix.f32 $R\\raw-$frame-block56.f32 480 272 0"
remote "powershell.exe -NoProfile -ExecutionPolicy Bypass -File $R\\run_dynamic_decoder_raw57_61.ps1 -Frame $frame"
upsample 62 61 8 128 64 272 480
remote "cmd /c $R\\d3d12_swin_chain.exe $R\\raw-$frame-block62-prefix.f32 $R\\raw-$frame-block65.f32 $R\\block62-body-effective.bin $R\\block63-body-effective.bin $R\\block64-body-effective.bin $R\\block65-body-effective.bin"
upsample 66 65 4 64 32 544 960
remote "cmd /c $R\\d3d12_swin32_chain.exe $R\\raw-$frame-block66-prefix.f32 $R\\raw-$frame-block69.f32 $R\\block66-body-effective.bin $R\\block67-body-effective.bin $R\\block68-body-effective.bin $R\\block69-body-effective.bin"

python3 "$root/decode_r10g10b10a2.py" "$work/backbuffer.bin" "$work/backbuffer-rgba.f32"
push "$work/backbuffer-rgba.f32" "raw-$frame-backbuffer-rgba.f32"
remote "cmd /c $R\\d3d12_block70_chain.exe $R\\raw-$frame-block69.f32 $R\\raw-$frame-block0-hwc.f32 $R\\raw-$frame-backbuffer-rgba.f32 $R\\raw-$frame-dlss5-rgba.f32"
pull "raw-$frame-dlss5-rgba.f32" "$work/output-rgba.f32"
python3 "$root/pack_r10g10b10a2.py" "$work/output-rgba.f32" "$work/output-r10.bin"
push "$work/output-r10.bin" dlss5-output-r10.new
remote "powershell.exe -NoProfile -Command \"Move-Item -Force $R\\dlss5-output-r10.new $R\\dlss5-output-r10.bin\""
python3 - "$work/output-rgba.f32" "$root/dynamic-captures/dynamic-frame-$frame-dlss5-synchronous.png" <<'PY'
import sys,numpy as np
from PIL import Image
a=np.fromfile(sys.argv[1],'<f4').reshape(2160,3840,4)
Image.fromarray(np.rint(np.clip(a[...,:3],0,1)*255).astype(np.uint8),'RGB').save(sys.argv[2])
PY
sha256sum "$work/output-r10.bin"
