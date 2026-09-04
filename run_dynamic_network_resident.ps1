param([int]$Frame)
$ErrorActionPreference='Stop'
$L='D:\DLSSNR-Lab'
function P($Block,$Suffix=''){"$L\raw-$Frame-block$Block$Suffix.f32"}
function Check($Name){if($LASTEXITCODE){throw "$Name failed: $LASTEXITCODE"}}
$Clock=[Diagnostics.Stopwatch]::StartNew();$Last=0
function Mark($Name){$Now=$Clock.Elapsed.TotalMilliseconds;Write-Output ("stage_wall_ms {0} {1:F3}" -f $Name,($Now-$script:Last));$script:Last=$Now}

& powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$L\run_dynamic_encoder_raw14.ps1" -Frame $Frame
Check 'encoder0-14'
Mark 'encoder0-14'
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$L\run_dynamic_raw14_30.ps1" -Frame $Frame
Check 'encoder15-30'
Mark 'encoder15-30'
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$L\run_dynamic_vit_raw.ps1" -Frame $Frame
Check 'vit31-38'
Mark 'vit31-38'

& "$L\d3d12_swin_chain.exe" --block39 (P 38) (P 30 '-body') (P 47) `
  "$L\block40-logical-effective.bin" "$L\block41-logical-effective.bin" `
  "$L\block42-logical-effective.bin" "$L\block43-logical-effective.bin" `
  "$L\block44-logical-effective.bin" "$L\block45-logical-effective.bin" `
  "$L\block46-logical-effective.bin" "$L\block47-logical-effective.bin"
Check 'block39+decoder40-47'
Mark 'block39+decoder40-47'

& "$L\d3d12_swin_chain.exe" --upsample (P 47) (P 22 '-body') (P 55) `
  "$L\block48-body-effective.bin" "$L\block49-body-effective.bin" `
  "$L\block50-body-effective.bin" "$L\block51-body-effective.bin" `
  "$L\block52-body-effective.bin" "$L\block53-body-effective.bin" `
  "$L\block54-body-effective.bin" "$L\block55-body-effective.bin"
Check 'decoder48-55'
Mark 'block48+decoder49-55'

& "$L\d3d12_swin_chain.exe" --upsample (P 55) (P 14) (P 61) `
  "$L\block56-body-effective.bin" "$L\block57-body-effective.bin" `
  "$L\block58-body-effective.bin" "$L\block59-body-effective.bin" `
  "$L\block60-body-effective.bin" "$L\block61-body-effective.bin"
Check 'decoder56-61'
Mark 'block56+decoder57-61'
& "$L\d3d12_swin_chain.exe" --upsample (P 61) (P 8) (P 65) `
  "$L\block62-body-effective.bin" "$L\block63-body-effective.bin" `
  "$L\block64-body-effective.bin" "$L\block65-body-effective.bin"
Check 'decoder62-65'
Mark 'block62-65'
& "$L\d3d12_swin32_chain.exe" --upsample (P 65) (P 4) (P 69) `
  "$L\block66-body-effective.bin" "$L\block67-body-effective.bin" `
  "$L\block68-body-effective.bin" "$L\block69-body-effective.bin"
Check 'decoder66-69'
Mark 'block66-69'

& "$L\d3d12_block70_chain.exe" (P 69) (P 0 '-hwc') `
  "$L\raw-$Frame-backbuffer-rgba.f32" "$L\raw-$Frame-dlss5-rgba.f32"
Check 'block70'
& "$L\pack_r10g10b10a2.exe" "$L\raw-$Frame-dlss5-rgba.f32" "$L\dlss5-output-r10.new"
Check 'pack R10'
Move-Item -Force "$L\dlss5-output-r10.new" "$L\dlss5-output-r10.bin"
Mark 'block70'
Write-Output ("network_wall_ms {0:F3}" -f $Clock.Elapsed.TotalMilliseconds)
