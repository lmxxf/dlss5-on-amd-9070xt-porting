param([string]$Folder='D:\DLSSNR-Lab\matrix-probe\native-rgb128',[uint32]$Seed=0,[switch]$Chain,[switch]$Tail,[switch]$Head,[switch]$Vit,[switch]$Decoder,[switch]$Upsample48,[switch]$Tail69,[switch]$FinalRGB,[ValidateSet(128,256,512)][int]$Width=128)
$ErrorActionPreference='Stop'
$env:DLSS5_SHADER_PROGRESS='1'
if ($FinalRGB -and (-not $Chain -or $Head -or $Vit -or $Decoder -or $Upsample48 -or $Tail69)) { throw '-FinalRGB requires -Chain and no other final-stage selector' }
if ($FinalRGB) { Remove-Item Env:DLSS5_POST_BASE_ONLY -ErrorAction SilentlyContinue }
if ($Tail69 -and (-not $Chain -or $Head -or $Vit -or $Decoder -or $Upsample48)) { throw '-Tail69 requires -Chain and no other final-stage selector' }
if ($Upsample48 -and (-not $Chain -or $Head -or $Vit -or $Decoder)) { throw '-Upsample48 requires -Chain and no other final-stage selector' }
if ($Decoder -and (-not $Chain -or $Head -or $Vit)) { throw '-Decoder requires -Chain and cannot combine with -Head/-Vit' }
if ($Tail -and -not $Chain) { throw '-Tail requires -Chain' }
if ($Head -and -not $Chain) { throw '-Head requires -Chain' }
if ($Vit -and -not $Chain) { throw '-Vit requires -Chain' }
if ($Vit -and $Head) { throw 'Choose -Vit or -Head, not both' }
$env:DLSS5_NOISE_TABLE=Join-Path $Folder 'functions.f32'
$env:DLSS5_TEST_SEED=$Seed.ToString()
$env:DLSS5_TEST_WIDTH=$Width.ToString()
if ((Get-Item $env:DLSS5_NOISE_TABLE).Length -ne 201326592) { throw 'Noise table size mismatch' }
if ($Chain) {
 if ($Tail) { $env:DLSS5_SPLIT_TAIL='1' } else { Remove-Item Env:DLSS5_SPLIT_TAIL -ErrorAction SilentlyContinue }
 if ($Seed -ne 0) { throw 'Current chain oracle only supports seed 0' }
 $mode=if ($FinalRGB) { 'rgb512final' } elseif ($Tail69) { 'rgb512tail69' } elseif ($Upsample48) { 'rgb512up48' } elseif ($Decoder) { 'rgb512decoder' } elseif ($Vit) { 'rgb512vit' } elseif ($Head) { 'rgb256head' } else { 'rgb128split' }
 & (Join-Path $Folder 'native-front-chain-test.exe') $Folder $mode
 if ($LASTEXITCODE -ne 0) { throw "Chain test failed: $LASTEXITCODE" }
 exit 0
}
& (Join-Path $Folder 'native-preblock-test.exe') (Join-Path $Folder 'block0-ffn.f32') (Join-Path $Folder 'block0-attention.f32') (Join-Path $Folder 'input.rgba32f') (Join-Path $Folder 'lut-main.f32') (Join-Path $Folder 'lut-down.f32') (Join-Path $Folder 'lut-raw.f32') $Folder live global
if ($LASTEXITCODE -ne 0) { throw "Preblock test failed: $LASTEXITCODE" }
