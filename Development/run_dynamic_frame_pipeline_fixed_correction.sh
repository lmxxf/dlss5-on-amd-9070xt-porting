#!/usr/bin/env bash
set -euo pipefail

frame=${1:?usage: run_dynamic_frame_pipeline.sh FRAME [SSH_HOST]}
host=${2:-amd9070}
lab='D:/DLSSNR-Lab'
tmp="/tmp/dlss5-dynamic-$frame"
mkdir -p "$tmp"
remote() { ssh "$host" "$@"; }
pull() { scp "$host:$lab/$1" "$2"; }
push() { scp "$1" "$host:$lab/$2"; }

pull "logs/ffx-color-$frame.bin" "$tmp/color.bin"
python3 prepare_dynamic_preblock_input.py "$tmp/color.bin" "$tmp/tiles.rgba32f" "$tmp/zero-block0.fp8"
push "$tmp/tiles.rgba32f" "ffx-color-$frame-tiles.rgba32f"
push "$tmp/zero-block0.fp8" "ffx-color-$frame-block0-zero.fp8"
remote "cmd /c D:\\DLSSNR-Lab\\d3d12_preblock_test.exe D:\\DLSSNR-Lab\\block0-distilled.bin D:\\DLSSNR-Lab\\ffx-color-$frame-tiles.rgba32f D:\\DLSSNR-Lab\\ffx-color-$frame-block0-zero.fp8 D:\\DLSSNR-Lab\\ffx-color-$frame-block0.f32"
pull "ffx-color-$frame-block0.f32" "$tmp/block0.f32"
python3 preblock_tiles_to_hwc.py "$tmp/block0.f32" tinlayout-2h64-output-permutation.i32 "$tmp/block0-hwc.f32" 1088 1920
push "$tmp/block0-hwc.f32" "ffx-color-$frame-block0-hwc.f32"

remote "powershell.exe -NoProfile -ExecutionPolicy Bypass -File D:\\DLSSNR-Lab\\run_dynamic_encoder.ps1 -Frame $frame"
remote "powershell.exe -NoProfile -ExecutionPolicy Bypass -File D:\\DLSSNR-Lab\\run_dynamic_encoder_tail.ps1 -Frame $frame"
remote "cmd /c D:\\DLSSNR-Lab\\d3d12_block128_test.exe D:\\DLSSNR-Lab\\block30-logical-effective.bin D:\\DLSSNR-Lab\\ffx-color-$frame-block29-corrected.f32 D:\\DLSSNR-Lab\\ffx-color-$frame-block30-body.f32 120 72 2 && D:\\DLSSNR-Lab\\d3d12_downsample_enter_test.exe D:\\DLSSNR-Lab\\ffx-color-$frame-block30-body.f32 D:\\DLSSNR-Lab\\block30-pool-identity.bin D:\\DLSSNR-Lab\\block30-enter-512x1024.bin D:\\DLSSNR-Lab\\ffx-color-$frame-block30-34x60.f32 120 68 512"
pull "ffx-color-$frame-block30-34x60.f32" "$tmp/block30-34x60.f32"
python3 pad_hwc_rows.py "$tmp/block30-34x60.f32" "$tmp/block30-padded.f32" 34 36 60 1024
push "$tmp/block30-padded.f32" "ffx-color-$frame-block30-padded.f32"
remote "cmd /c D:\\DLSSNR-Lab\\d3d12_affine_test.exe D:\\DLSSNR-Lab\\block30-live-correction.bin D:\\DLSSNR-Lab\\ffx-color-$frame-block30-padded.f32 D:\\DLSSNR-Lab\\ffx-color-$frame-block30-corrected.f32 1024 1024"
remote "powershell.exe -NoProfile -ExecutionPolicy Bypass -File D:\\DLSSNR-Lab\\run_dynamic_vit.ps1 -Frame $frame"

pull "ffx-color-$frame-block38-corrected.f32" "$tmp/block38.f32"
pull "ffx-color-$frame-block30-body.f32" "$tmp/block30-body.f32"
python3 prepare_block39_dynamic_input.py "$tmp/block38.f32" "$tmp/block30-body.f32" block39-logical-effective.bin "$tmp/block39-input.f32" "$tmp/block39-matrix.bin"
push "$tmp/block39-input.f32" "ffx-color-$frame-block39-input.f32"
push "$tmp/block39-matrix.bin" "block39-logical-effective-bias.bin"
remote "cmd /c D:\\DLSSNR-Lab\\d3d12_affine_test.exe D:\\DLSSNR-Lab\\block39-logical-effective-bias.bin D:\\DLSSNR-Lab\\ffx-color-$frame-block39-input.f32 D:\\DLSSNR-Lab\\ffx-color-$frame-block39.f32 1536 512"
remote "powershell.exe -NoProfile -ExecutionPolicy Bypass -File D:\\DLSSNR-Lab\\run_dynamic_decoder40_47.ps1 -Frame $frame"

remote "cmd /c D:\\DLSSNR-Lab\\d3d12_affine_test.exe D:\\DLSSNR-Lab\\block48-prefix-matrix-with-bias.bin D:\\DLSSNR-Lab\\ffx-color-$frame-block47.f32 D:\\DLSSNR-Lab\\ffx-color-$frame-block48-projected.f32 512 256"
pull "ffx-color-$frame-block48-projected.f32" "$tmp/block48-projected.f32"
pull "ffx-color-$frame-block22-body.f32" "$tmp/block22-body.f32"
python3 prepare_block48_dynamic.py /home/lmxxf/work/tmp-test/block48.weights block48-prefix-matrix-with-bias.bin --projected "$tmp/block48-projected.f32" --skip "$tmp/block22-body.f32" --output "$tmp/block48-prefix.f32"
push "$tmp/block48-prefix.f32" "ffx-color-$frame-block48-prefix.f32"
remote "cmd /c D:\\DLSSNR-Lab\\d3d12_block128_test.exe D:\\DLSSNR-Lab\\block48-body-effective.bin D:\\DLSSNR-Lab\\ffx-color-$frame-block48-prefix.f32 D:\\DLSSNR-Lab\\ffx-color-$frame-block48.f32 240 136 0 && D:\\DLSSNR-Lab\\d3d12_affine_test.exe D:\\DLSSNR-Lab\\block48-live-correction.bin D:\\DLSSNR-Lab\\ffx-color-$frame-block48.f32 D:\\DLSSNR-Lab\\ffx-color-$frame-block48-corrected.f32 256 256"
remote "powershell.exe -NoProfile -ExecutionPolicy Bypass -File D:\\DLSSNR-Lab\\run_dynamic_decoder49_55.ps1 -Frame $frame"

for spec in '56 55 14 256 128 136 240' '62 61 8 128 64 272 480' '66 65 4 64 32 544 960'; do
  read -r block previous skip main_channels output_channels height width <<<"$spec"
  remote "cmd /c D:\\DLSSNR-Lab\\d3d12_affine_test.exe D:\\DLSSNR-Lab\\block$block-prefix-matrix-with-bias.bin D:\\DLSSNR-Lab\\ffx-color-$frame-block$previous.f32 D:\\DLSSNR-Lab\\ffx-color-$frame-block$block-projected.f32 $main_channels $output_channels"
  pull "ffx-color-$frame-block$block-projected.f32" "$tmp/block$block-projected.f32"
  pull "ffx-color-$frame-block$skip.f32" "$tmp/block$skip-skip.f32"
  python3 prepare_decoder_upsample.py "/home/lmxxf/work/tmp-test/block$block.weights" "block$block-prefix-matrix-with-bias.bin" "$main_channels" "$output_channels" --projected "$tmp/block$block-projected.f32" --height "$height" --width "$width" --skip "$tmp/block$skip-skip.f32" --output "$tmp/block$block-prefix.f32"
  push "$tmp/block$block-prefix.f32" "ffx-color-$frame-block$block-prefix.f32"
  if [[ $block == 56 ]]; then
    remote "cmd /c D:\\DLSSNR-Lab\\d3d12_block128_test.exe D:\\DLSSNR-Lab\\block56-body-effective.bin D:\\DLSSNR-Lab\\ffx-color-$frame-block56-prefix.f32 D:\\DLSSNR-Lab\\ffx-color-$frame-block56.f32 480 272 0 && D:\\DLSSNR-Lab\\d3d12_affine_test.exe D:\\DLSSNR-Lab\\block56-live-correction.bin D:\\DLSSNR-Lab\\ffx-color-$frame-block56.f32 D:\\DLSSNR-Lab\\ffx-color-$frame-block56-corrected.f32 128 128"
    remote "powershell.exe -NoProfile -ExecutionPolicy Bypass -File D:\\DLSSNR-Lab\\run_dynamic_decoder57_61.ps1 -Frame $frame"
  elif [[ $block == 62 ]]; then
    remote "cmd /c D:\\DLSSNR-Lab\\d3d12_block128_test.exe D:\\DLSSNR-Lab\\block62-body-effective.bin D:\\DLSSNR-Lab\\ffx-color-$frame-block62-prefix.f32 D:\\DLSSNR-Lab\\ffx-color-$frame-block62.f32 960 544 0 && D:\\DLSSNR-Lab\\d3d12_affine_test.exe D:\\DLSSNR-Lab\\block62-from-amd61-correction.bin D:\\DLSSNR-Lab\\ffx-color-$frame-block62.f32 D:\\DLSSNR-Lab\\ffx-color-$frame-block62-corrected.f32 64 64 && D:\\DLSSNR-Lab\\d3d12_block128_test.exe D:\\DLSSNR-Lab\\block63-body-effective.bin D:\\DLSSNR-Lab\\ffx-color-$frame-block62-corrected.f32 D:\\DLSSNR-Lab\\ffx-color-$frame-block63.f32 960 544 0 && D:\\DLSSNR-Lab\\d3d12_affine_test.exe D:\\DLSSNR-Lab\\block63-live-correction.bin D:\\DLSSNR-Lab\\ffx-color-$frame-block63.f32 D:\\DLSSNR-Lab\\ffx-color-$frame-block63-corrected.f32 64 64 && D:\\DLSSNR-Lab\\d3d12_block128_test.exe D:\\DLSSNR-Lab\\block64-body-effective.bin D:\\DLSSNR-Lab\\ffx-color-$frame-block63-corrected.f32 D:\\DLSSNR-Lab\\ffx-color-$frame-block64.f32 960 544 1 && D:\\DLSSNR-Lab\\d3d12_affine_test.exe D:\\DLSSNR-Lab\\block64-live-correction.bin D:\\DLSSNR-Lab\\ffx-color-$frame-block64.f32 D:\\DLSSNR-Lab\\ffx-color-$frame-block64-corrected.f32 64 64 && D:\\DLSSNR-Lab\\d3d12_block128_test.exe D:\\DLSSNR-Lab\\block65-body-effective.bin D:\\DLSSNR-Lab\\ffx-color-$frame-block64-corrected.f32 D:\\DLSSNR-Lab\\ffx-color-$frame-block65.f32 960 544 3"
  else
    remote "cmd /c D:\\DLSSNR-Lab\\d3d12_block1_test.exe D:\\DLSSNR-Lab\\block66-body-effective.bin D:\\DLSSNR-Lab\\ffx-color-$frame-block66-prefix.f32 D:\\DLSSNR-Lab\\ffx-color-$frame-block66.f32 1920 1088 0 && D:\\DLSSNR-Lab\\d3d12_affine_test.exe D:\\DLSSNR-Lab\\block66-from-amd65-correction.bin D:\\DLSSNR-Lab\\ffx-color-$frame-block66.f32 D:\\DLSSNR-Lab\\ffx-color-$frame-block66-corrected.f32 32 32 && D:\\DLSSNR-Lab\\d3d12_block1_test.exe D:\\DLSSNR-Lab\\block67-body-effective.bin D:\\DLSSNR-Lab\\ffx-color-$frame-block66-corrected.f32 D:\\DLSSNR-Lab\\ffx-color-$frame-block67.f32 1920 1088 1 && D:\\DLSSNR-Lab\\d3d12_block1_test.exe D:\\DLSSNR-Lab\\block68-body-effective.bin D:\\DLSSNR-Lab\\ffx-color-$frame-block67.f32 D:\\DLSSNR-Lab\\ffx-color-$frame-block68.f32 1920 1088 1 && D:\\DLSSNR-Lab\\d3d12_block1_test.exe D:\\DLSSNR-Lab\\block69-body-effective.bin D:\\DLSSNR-Lab\\ffx-color-$frame-block68.f32 D:\\DLSSNR-Lab\\ffx-color-$frame-block69.f32 1920 1088 0"
  fi
done

pull "ffx-color-$frame-block69.f32" "$tmp/block69.f32"
python3 prepare_block70_spatial_input.py "$tmp/block69.f32" "$tmp/block0-hwc.f32" block69-to70-live-main-correction.bin "$tmp/block70-input.f32" --full
push "$tmp/block70-input.f32" "ffx-color-$frame-block70-input-full.f32"
remote "cmd /c D:\\DLSSNR-Lab\\d3d12_block70_spatial_test.exe D:\\DLSSNR-Lab\\block70-spatial-effective.bin D:\\DLSSNR-Lab\\ffx-color-$frame-block70-input-full.f32 D:\\DLSSNR-Lab\\ffx-color-1200-block70-zero.f32 D:\\DLSSNR-Lab\\ffx-color-$frame-block70-rgb-full.f32 3840 2176"
pull "ffx-color-$frame-block70-rgb-full.f32" "$tmp/block70-rgb.f32"
python3 compose_dynamic_dlss5.py "$tmp/color.bin" "$tmp/block70-rgb.f32" "$tmp/output.png" --raw-output "$tmp/output.f32"
python3 pack_r10g10b10a2.py "$tmp/output.f32" "$tmp/output-r10.bin"
push "$tmp/output-r10.bin" 'dlss5-output-r10.new'
remote "powershell.exe -NoProfile -Command \"Move-Item -Force D:\\DLSSNR-Lab\\dlss5-output-r10.new D:\\DLSSNR-Lab\\dlss5-output-r10.bin\""
cp "$tmp/output.png" "dynamic-captures/dynamic-frame-$frame-dlss5-composited.png"
sha256sum "$tmp/output-r10.bin"
