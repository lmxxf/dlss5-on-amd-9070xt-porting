param([string]$Lab = 'D:\DLSSNR-Lab', [switch]$NativeHalf, [switch]$PairProject)
$ErrorActionPreference = 'Stop'
if ($PairProject -and -not $NativeHalf) { throw 'PairProject requires NativeHalf' }
$Dxc = Join-Path $Lab 'dxc\bin\x64\dxc.exe'
foreach ($Pass in @('ffn','qkv','attention')) {
    $Source = switch ($Pass) {'ffn' {'block1_ffn_sm6_fp32.hlsl'} 'qkv' {'block70_qkv_parallel.hlsl'} 'attention' {'block70_attention_shared.hlsl'}}
    $Entry = if ($Pass -eq 'ffn') {'ffn'} else {'main'}
    $Extra = if ($NativeHalf) {@('-enable-16bit-types','-D','NATIVE_HALF=1')} else {@()}
    if ($PairProject) { $Extra += @('-D','PAIR_PROJECT=1') }
    & $Dxc -T cs_6_2 -E $Entry -O3 -D PACKED_FEATURE=1 -D CACHE_SCORES=2 @Extra (Join-Path $Lab $Source) -Fo (Join-Path $Lab "shader-cache\block70-packed-feature-$Pass.cso")
    if ($LASTEXITCODE) { throw "Packed feature compilation failed: $Pass" }
}
