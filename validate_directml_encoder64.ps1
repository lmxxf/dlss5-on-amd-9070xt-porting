param([string]$Block8 = 'D:\DLSSNR-Lab\directml-encoder64-block8.f32')
$ErrorActionPreference = 'Stop'
$L = 'D:\DLSSNR-Lab'
function V($Name) { "$L\encoder64-validation-$Name.f32" }
function Check($Name) { if ($LASTEXITCODE) { throw "$Name failed: $LASTEXITCODE" } }

& "$L\d3d12_swin_chain.exe" $Block8 (V 'block14') `
  "$L\block9-body-effective.bin" "$L\block10-effective.bin" `
  "$L\block11-effective.bin" "$L\block12-effective.bin" `
  "$L\block13-effective.bin" "$L\block14-body-effective.bin"
Check 'blocks9-14'

& "$L\d3d12_swin_chain.exe" (V 'block14') (V 'block22') `
  "$L\block15-body-effective.bin" "$L\block16-body-effective.bin" `
  "$L\block17-body-effective.bin" "$L\block18-body-effective.bin" `
  "$L\block19-body-effective.bin" "$L\block20-body-effective.bin" `
  "$L\block21-body-effective.bin" "$L\block22-body-effective.bin"
Check 'blocks15-22'

& "$L\d3d12_swin_chain.exe" (V 'block22') (V 'block30') `
  "$L\block23-logical-effective.bin" "$L\block24-logical-effective.bin" `
  "$L\block25-logical-effective.bin" "$L\block26-logical-effective.bin" `
  "$L\block27-logical-effective.bin" "$L\block28-logical-effective.bin" `
  "$L\block29-logical-effective.bin" "$L\block30-logical-effective.bin"
Check 'blocks23-30'

$env:DML_VIT_WEIGHT_DIR = $L
$env:DML_VIT8_OUTPUT = V 'block38'
$env:DML_BLOCK30_BODY = V 'block30'
$env:DML_BLOCK30_POOL = "$L\block30-pool-identity.bin"
$env:DML_BLOCK30_ENTER = "$L\block30-enter-512x1024.bin"
$env:DML_TRUE_EXPAND_WEIGHT = "$L\block31-vit-expand-effective.f16"
$env:DML_TRUE_CONTRACT_WEIGHT = "$L\block31-vit-contract.f16"
$env:DML_TRUE_CONTRACT_SKIP = "$L\block31-vit-contract-skip.f16"
$env:DML_TRUE_PROJECTION_WEIGHT = "$L\block31-vit-projection.f16"
$env:DML_TRUE_PROJECTION_SKIP = "$L\block31-vit-projection-skip.f16"
& "$L\d3d12_directml_vit_resident.exe"
Check 'blocks31-38'

& "$L\d3d12_swin_chain.exe" --block39 (V 'block38') (V 'block30') (V 'block47') `
  "$L\block40-logical-effective.bin" "$L\block41-logical-effective.bin" `
  "$L\block42-logical-effective.bin" "$L\block43-logical-effective.bin" `
  "$L\block44-logical-effective.bin" "$L\block45-logical-effective.bin" `
  "$L\block46-logical-effective.bin" "$L\block47-logical-effective.bin"
Check 'blocks39-47'

& "$L\d3d12_swin_chain.exe" --upsample (V 'block47') (V 'block22') (V 'block55') `
  "$L\block48-body-effective.bin" "$L\block49-body-effective.bin" `
  "$L\block50-body-effective.bin" "$L\block51-body-effective.bin" `
  "$L\block52-body-effective.bin" "$L\block53-body-effective.bin" `
  "$L\block54-body-effective.bin" "$L\block55-body-effective.bin"
Check 'blocks48-55'

& "$L\d3d12_swin_chain.exe" --upsample (V 'block55') (V 'block14') (V 'block61') `
  "$L\block56-body-effective.bin" "$L\block57-body-effective.bin" `
  "$L\block58-body-effective.bin" "$L\block59-body-effective.bin" `
  "$L\block60-body-effective.bin" "$L\block61-body-effective.bin"
Check 'blocks56-61'

& "$L\d3d12_swin_chain.exe" --upsample (V 'block61') $Block8 (V 'block65') `
  "$L\block62-body-effective.bin" "$L\block63-body-effective.bin" `
  "$L\block64-body-effective.bin" "$L\block65-body-effective.bin"
Check 'blocks62-65'

& "$L\d3d12_swin32_chain.exe" --upsample (V 'block65') "$L\raw-16800-block4.f32" (V 'block69') `
  "$L\block66-body-effective.bin" "$L\block67-body-effective.bin" `
  "$L\block68-body-effective.bin" "$L\block69-body-effective.bin"
Check 'blocks66-69'

$Output = "$L\encoder64-validation-r10.bin"
& "$L\d3d12_block70_chain.exe" (V 'block69') "$L\raw-16800-block0-hwc.f32" `
  "$L\raw-16800-backbuffer-rgba.f32" $Output
Check 'block70'

Get-FileHash $Output, "$L\old-swin64-r10.bin" -Algorithm SHA256 |
  Select-Object Path, Hash
