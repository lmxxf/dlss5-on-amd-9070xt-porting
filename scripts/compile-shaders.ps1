# Compiles every shader of the fast chain on any Windows x64 machine (no GPU needed): copies shaders\ from this repository
# into <Folder> and runs bench.ps1 -CompileOnly there with the Shader Model 6.10 preview dxc (the DirectX preview package,
# dxc.exe + inc\hlsl\dx\linalg.h; not in this repository). The .cso files land next to the sources in <Folder>;
# deploy_fast.ps1 -Source <Folder> installs them. usage: powershell -File scripts\compile-shaders.ps1 -Folder D:\dlss5-shaders -DxcRoot D:\dxc-preview
param([Parameter(Mandatory=$true)][string]$Folder,[Parameter(Mandatory=$true)][string]$DxcRoot)
$ErrorActionPreference='Stop'
$Repo=Split-Path -Parent $PSScriptRoot
if(-not (Test-Path (Join-Path $DxcRoot 'bin\x64\dxc.exe'))){throw "dxc.exe not found under $DxcRoot\bin\x64"}
New-Item -ItemType Directory -Force $Folder | Out-Null
Copy-Item (Join-Path $Repo 'shaders\*') $Folder -Force
Copy-Item (Join-Path $Repo 'scripts\bench.ps1') $Folder -Force
& (Join-Path $Folder 'bench.ps1') -Folder $Folder -DxcRoot $DxcRoot -CompileOnly
