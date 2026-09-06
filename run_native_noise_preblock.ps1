param([string]$Folder='D:\DLSSNR-Lab\matrix-probe\native-rgb128',[uint32]$Seed=0,[switch]$Chain,[switch]$Tail,[switch]$Head,[switch]$Vit)
$ErrorActionPreference='Stop'
if ($Tail -and -not $Chain) { throw '-Tail requires -Chain' }
if ($Head -and -not $Chain) { throw '-Head requires -Chain' }
if ($Vit -and -not $Chain) { throw '-Vit requires -Chain' }
if ($Vit -and $Head) { throw 'Choose -Vit or -Head, not both' }
$env:DLSS5_NOISE_TABLE=Join-Path $Folder 'functions.f32'
$env:DLSS5_TEST_SEED=$Seed.ToString()
if ((Get-Item $env:DLSS5_NOISE_TABLE).Length -ne 201326592) { throw 'Noise table size mismatch' }
if ($Chain) {
 if ($Tail) { $env:DLSS5_SPLIT_TAIL='1' } else { Remove-Item Env:DLSS5_SPLIT_TAIL -ErrorAction SilentlyContinue }
 if ($Seed -ne 0) { throw 'Current chain oracle only supports seed 0' }
 $mode=if ($Vit) { 'rgb512vit' } elseif ($Head) { 'rgb256head' } else { 'rgb128split' }
 & (Join-Path $Folder 'native-front-chain-test.exe') $Folder $mode
 if ($LASTEXITCODE -ne 0) { throw "Chain test failed: $LASTEXITCODE" }
 exit 0
}
& (Join-Path $Folder 'native-preblock-test.exe') (Join-Path $Folder 'block0-ffn.f32') (Join-Path $Folder 'block0-attention.f32') (Join-Path $Folder 'input.rgba32f') (Join-Path $Folder 'lut-main.f32') (Join-Path $Folder 'lut-down.f32') (Join-Path $Folder 'lut-raw.f32') $Folder live global
if ($LASTEXITCODE -ne 0) { throw "Preblock test failed: $LASTEXITCODE" }
