param([string]$Folder='D:\DLSSNR-Lab\matrix-probe\native-reflect-preblock',[string]$Noise='D:\DLSSNR-Lab\matrix-probe\native-runtime-rgb512\functions.f32',[switch]$Front4,[switch]$Front8,[switch]$Front14,[switch]$Front22)
$ErrorActionPreference='Stop'
if((Get-Item $Noise).Length -ne 201326592){throw 'Noise table size mismatch'}
$env:DLSS5_NOISE_TABLE=$Noise
$env:DLSS5_TEST_WIDTH='1920';$env:DLSS5_TEST_SEED='0';$env:DLSS5_REFLECT_VALID1080='1'
if($Front22){$env:DLSS5_FRONT22='1'}else{Remove-Item Env:DLSS5_FRONT22 -ErrorAction SilentlyContinue}
if($Front4){$env:DLSS5_FRONT4='1'}else{Remove-Item Env:DLSS5_FRONT4 -ErrorAction SilentlyContinue}
if($Front14){$env:DLSS5_FRONT14='1'}else{Remove-Item Env:DLSS5_FRONT14 -ErrorAction SilentlyContinue}
if($Front8){$env:DLSS5_FRONT8='1'}else{Remove-Item Env:DLSS5_FRONT8 -ErrorAction SilentlyContinue}
& "$Folder\native-preblock-test.exe" "$Folder\block0-ffn.f32" "$Folder\block0-attention.f32" "$Folder\input.f32" "$Folder\gpu-main.f32" "$Folder\gpu-down.f32" "$Folder\gpu-raw.f32" $Folder live global
if($LASTEXITCODE -ne 0){throw "Reflected preblock failed: $LASTEXITCODE"}
