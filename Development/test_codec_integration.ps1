param([switch]$Amd)
$ErrorActionPreference = 'Stop'
Push-Location (Join-Path $PSScriptRoot '..')
try {
    # Run from an MSVC x64 developer shell. All generated files remain ignored.
    if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) { throw 'MSVC x64 developer shell required' }
    $out = Join-Path $PSScriptRoot 'temp/codec-integration'
    $legacy = Join-Path $out 'legacy'
    New-Item -ItemType Directory -Force $legacy | Out-Null
    foreach ($stage in @('encode', 'decode')) {
        # Pre-PR shader implementation: independent compatibility oracle, not a
        # second implementation of the new formulas inside the test.
        $source = & git show "54e14de5:shaders/native_codec_$stage.hlsl"
        if ($LASTEXITCODE) { throw 'Pre-PR reference commit 54e14de5 is required locally' }
        [IO.File]::WriteAllText((Join-Path $legacy "native_codec_$stage.hlsl"), ($source -join "`n"))
    }
    & cl.exe /nologo /std:c++17 /utf-8 /EHsc /O2 /DNOMINMAX /I src `
        Development/d3d12_codec_integration_test.cpp "/Fo:$out/test.obj" "/Fe:$out/test.exe" `
        d3d12.lib dxgi.lib d3dcompiler.lib dxguid.lib
    if ($LASTEXITCODE) { throw 'Codec integration test compilation failed' }
    & "$out/test.exe" "$PWD/shaders" $legacy
    if ($LASTEXITCODE) { throw 'WARP codec integration test failed' }
    if ($Amd) {
        & "$out/test.exe" "$PWD/shaders" $legacy amd
        if ($LASTEXITCODE) { throw 'AMD codec integration test failed' }
    }
} finally { Pop-Location }
