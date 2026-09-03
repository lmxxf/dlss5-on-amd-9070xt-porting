param([int]$Frame)
$ErrorActionPreference='Stop'
$L='D:\DLSSNR-Lab'
function P($Block,$Suffix=''){"$L\raw-$Frame-block$Block$Suffix.f32"}
function Check($Name){if($LASTEXITCODE){throw "$Name failed: $LASTEXITCODE"}}

& powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$L\run_dynamic_encoder_raw13.ps1" -Frame $Frame
Check 'encoder0-13'
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$L\run_dynamic_raw14_30.ps1" -Frame $Frame
Check 'encoder14-30'
& "$L\pad_hwc_rows.exe" (P 30 '-34x60') (P 30) 34 36 60 1024
Check 'pad block30'
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$L\run_dynamic_vit_raw.ps1" -Frame $Frame
Check 'vit31-38'

& "$L\prepare_block39_dynamic_input.exe" (P 38) (P 30 '-body') (P 39 '-input')
Check 'prepare block39'
& "$L\d3d12_affine_test.exe" "$L\block39-logical-effective-bias.bin" (P 39 '-input') (P 39) 1536 512
Check 'block39'
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$L\run_dynamic_decoder_raw40_47.ps1" -Frame $Frame
Check 'decoder40-47'

& "$L\d3d12_affine_test.exe" "$L\block48-prefix-matrix-with-bias.bin" (P 47) (P 48 '-projected') 512 256
Check 'block48 projection'
& "$L\merge_upsample_skip.exe" (P 48 '-projected') (P 22 '-body') (P 48 '-prefix') 68 120 256
Check 'block48 merge'
& "$L\d3d12_block128_test.exe" "$L\block48-body-effective.bin" (P 48 '-prefix') (P 48) 240 136 0
Check 'block48 body'
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$L\run_dynamic_decoder_raw49_55.ps1" -Frame $Frame
Check 'decoder49-55'

function Upsample($Block,$Previous,$Skip,$InputChannels,$OutputChannels,$Height,$Width){
  & "$L\d3d12_affine_test.exe" "$L\block$Block-prefix-matrix-with-bias.bin" (P $Previous) (P $Block '-projected') $InputChannels $OutputChannels
  Check "block$Block projection"
  & "$L\merge_upsample_skip.exe" (P $Block '-projected') (P $Skip) (P $Block '-prefix') $Height $Width $OutputChannels
  Check "block$Block merge"
}
Upsample 56 55 14 256 128 136 240
& "$L\d3d12_block128_test.exe" "$L\block56-body-effective.bin" (P 56 '-prefix') (P 56) 480 272 0
Check 'block56 body'
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$L\run_dynamic_decoder_raw57_61.ps1" -Frame $Frame
Check 'decoder57-61'
Upsample 62 61 8 128 64 272 480
& "$L\d3d12_swin_chain.exe" (P 62 '-prefix') (P 65) `
  "$L\block62-body-effective.bin" "$L\block63-body-effective.bin" `
  "$L\block64-body-effective.bin" "$L\block65-body-effective.bin"
Check 'decoder62-65'
Upsample 66 65 4 64 32 544 960
& "$L\d3d12_swin32_chain.exe" (P 66 '-prefix') (P 69) `
  "$L\block66-body-effective.bin" "$L\block67-body-effective.bin" `
  "$L\block68-body-effective.bin" "$L\block69-body-effective.bin"
Check 'decoder66-69'

& "$L\d3d12_block70_chain.exe" (P 69) (P 0 '-hwc') `
  "$L\raw-$Frame-backbuffer-rgba.f32" "$L\raw-$Frame-dlss5-rgba.f32"
Check 'block70'
