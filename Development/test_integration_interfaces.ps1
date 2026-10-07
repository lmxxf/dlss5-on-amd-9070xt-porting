param([switch]$Amd, [string]$Assets, [string]$Modules)
$ErrorActionPreference = 'Stop'
if ([bool]$Assets -ne [bool]$Modules) { throw 'Provide both -Assets and -Modules (architecture-specific directory)' }
if ($Assets -and -not $Amd) { throw 'The optional full-network auxiliary test requires -Amd' }
if ($Assets) {
    $Assets = (Resolve-Path -LiteralPath $Assets).Path
    $Modules = (Resolve-Path -LiteralPath $Modules).Path
}
Push-Location (Join-Path $PSScriptRoot '..')
try {
    if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) { throw 'MSVC x64 developer shell required' }
    & "$PSScriptRoot/test_codec_integration.ps1" -Amd:$Amd
    $out = Join-Path $PWD 'exports/integration-interfaces'
    New-Item -ItemType Directory -Force $out | Out-Null
    & cl.exe /nologo /std:c++17 /EHsc /utf-8 /DNOMINMAX /I src /c Development/HIP/bridge_network.cpp "/Fo:$out/bridge.obj"
    if ($LASTEXITCODE) { throw 'Bridge compilation failed' }
    & cl.exe /nologo /std:c++17 /EHsc /utf-8 Development/HIP/test_module_load.cpp "/Fe:$out/module.exe" "/Fo:$out/module.obj"
    if ($LASTEXITCODE) { throw 'Module ownership test compilation failed' }
    & "$out/module.exe" Development/HIP/test_module_load.cpp
    if ($LASTEXITCODE) { throw 'Module ownership test failed' }
    & cl.exe /nologo /std:c++17 /O2 /EHsc /utf-8 /I src /I Development/HIP Development/HIP/test_multipass_aux.cpp "/Fe:$out/multipass-aux.exe" "/Fo:$out/multipass-aux.obj" user32.lib
    if ($LASTEXITCODE) { throw 'Multipass auxiliary test compilation failed' }
    & cl.exe /nologo /std:c++17 /O2 /EHsc /utf-8 /DHIP_MP_RAW_EXPORT /I src /I Development/HIP Development/HIP/test_multipass_aux.cpp "/Fe:$out/raw-export-aux.exe" "/Fo:$out/raw-export-aux.obj" user32.lib
    if ($LASTEXITCODE) { throw 'Raw-export auxiliary guard compilation failed' }
    if ($Assets) {
        & "$out/multipass-aux.exe" $Assets $Modules
        if ($LASTEXITCODE) { throw 'Multipass auxiliary GPU test failed' }
        & "$out/raw-export-aux.exe" $Assets $Modules
        if ($LASTEXITCODE) { throw 'Raw-export auxiliary guard GPU test failed' }
    } else { Write-Host 'SKIP full-network auxiliary GPU test: supply -Amd -Assets <weights> -Modules <arch modules>' }
    & cl.exe /nologo /std:c++17 /EHsc /utf-8 /I src Development/test_fast_history.cpp "/Fe:$out/history.exe" "/Fo:$out/history.obj" d3d12.lib dxgi.lib d3dcompiler.lib dxguid.lib
    if ($LASTEXITCODE) { throw 'Fast History test compilation failed' }
    & cl.exe /nologo /std:c++17 /EHsc /utf-8 /DDLSS5_USE_HIP /I src Development/test_addon_fast_history.cpp "/Fe:$out/addon.exe" "/Fo:$out/addon.obj" d3d12.lib dxgi.lib d3dcompiler.lib dxguid.lib user32.lib
    if ($LASTEXITCODE) { throw 'Addon History compilation failed' }
    & "$out/addon.exe"
    if ($LASTEXITCODE) { throw 'Addon deferred History failed' }
    & "$out/history.exe"
    if ($LASTEXITCODE) { throw 'Fast History WARP failed' }
    if ($Amd) {
        & "$out/addon.exe" --hardware
        if ($LASTEXITCODE) { throw 'Addon deferred GPU History failed' }
        & "$out/history.exe" --hardware
        if ($LASTEXITCODE) { throw 'Fast History GPU failed' }
    }
} finally { Pop-Location }
