param([string]$Lab = 'D:\DLSSNR-Lab')
$ErrorActionPreference = 'Stop'
$Dxc = Join-Path $Lab 'dxc\bin\x64\dxc.exe'
$Cache = Join-Path $Lab 'shader-cache'
if (!(Test-Path $Dxc)) { throw "DXC not found: $Dxc" }
New-Item -ItemType Directory -Force $Cache | Out-Null

function Compile-Ffn($Source, $Prefix, $Width, $Height, $Shifts) {
    $Output = Join-Path $Cache "${Prefix}-${Width}x${Height}-s0-ffn.cso"
    & $Dxc -T cs_6_2 -E ffn -O3 -D "WIDTH=$Width" -D "HEIGHT=$Height" `
        (Join-Path $Lab $Source) -Fo $Output
    if ($LASTEXITCODE) { throw "SM6 FP32 FFN compilation failed: $Source" }
    foreach ($Shift in $Shifts) {
        if ($Shift -ne 0) {
            Copy-Item -Force $Output (Join-Path $Cache "${Prefix}-${Width}x${Height}-s$Shift-ffn.cso")
        }
    }
    Get-FileHash $Output -Algorithm SHA256
}

function Compile-Parallel128($Width, $Height, $Shifts) {
    $Source = Join-Path $Lab 'block128_ffn_sm6_fp32.hlsl'
    $Expand = Join-Path $Cache "block128-v2-c128-${Width}x${Height}-s0-ffn-expand.cso"
    $Project = Join-Path $Cache "block128-v2-c128-${Width}x${Height}-s0-ffn-project.cso"
    & $Dxc -T cs_6_2 -E ffn_expand -O3 -D "WIDTH=$Width" -D "HEIGHT=$Height" $Source -Fo $Expand
    if ($LASTEXITCODE) { throw 'SM6 FP32 128-channel expand compilation failed' }
    & $Dxc -T cs_6_2 -E ffn_project -O3 -D "WIDTH=$Width" -D "HEIGHT=$Height" $Source -Fo $Project
    if ($LASTEXITCODE) { throw 'SM6 FP32 128-channel project compilation failed' }
    foreach ($Shift in $Shifts) {
        if ($Shift -ne 0) {
            Copy-Item -Force $Expand (Join-Path $Cache "block128-v2-c128-${Width}x${Height}-s$Shift-ffn-expand.cso")
            Copy-Item -Force $Project (Join-Path $Cache "block128-v2-c128-${Width}x${Height}-s$Shift-ffn-project.cso")
        }
    }
    Get-FileHash $Expand, $Project -Algorithm SHA256
}

# DXC/SM6 gives the speedup while FP32 accumulation preserves the old
# D3DCompile output byte-for-byte. QKV/attention remain cached FP32 shaders.
$Hashes = @()
$Hashes += Compile-Ffn 'block1_ffn_sm6_fp32.hlsl' 'block1-v1' 1920 1088 @(0, 1)
$Hashes += Compile-Ffn 'block1_ffn_sm6_fp32.hlsl' 'block1-v1' 960 544 @(0, 1)
$Hashes += Compile-Ffn 'block64_ffn_sm6_fp32.hlsl' 'block128-v1-c64' 960 544 @(0, 1, 2, 3)
$Hashes += Compile-Parallel128 480 272 @(0, 1, 2, 3)
$Hashes | Format-Table Path, Hash
