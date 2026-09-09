param([Parameter(Mandatory=$true)][ValidatePattern('^[0-9a-fA-F]{64}$')][string]$ExpectedHash)
$ErrorActionPreference='Stop'
$Lab='D:\DLSSNR-Lab'
$Game='C:\Program Files (x86)\Steam\steamapps\common\StellarBlade\SB\Binaries\Win64'
if(Get-Process SB-Win64-Shipping -ErrorAction SilentlyContinue){throw 'Exit game before deployment'}
$Source=Join-Path $Lab 'dlss5-sdk721.addon64'
if((Get-FileHash -LiteralPath $Source -Algorithm SHA256).Hash -ne $ExpectedHash){throw 'Development DLL hash mismatch'}
$Target=Join-Path $Game 'dlss5-1080p-runtime.addon64'
$Backup=Join-Path $Lab 'matrix-probe\game-runtime-before-matrix.addon64'
if(-not(Test-Path -LiteralPath $Backup)){
    if((Get-FileHash -LiteralPath $Target -Algorithm SHA256).Hash -ne 'CA668B23D81DD06964053D13A03315FBA3BC04A920B93AC070C391058AF6B415'){throw 'Unexpected original game addon; inspect before backup'}
    Copy-Item -LiteralPath $Target -Destination $Backup
}
if((Get-FileHash -LiteralPath $Backup -Algorithm SHA256).Hash -ne 'CA668B23D81DD06964053D13A03315FBA3BC04A920B93AC070C391058AF6B415'){throw 'Baseline backup hash mismatch'}
$Sdk=Join-Path $Game 'DLSS5-D3D12-721'
[void][IO.Directory]::CreateDirectory($Sdk)
foreach($Name in @('D3D12Core.dll','d3d12SDKLayers.dll')){
    $Original=Join-Path $Lab ('matrix-probe\D3D12\'+$Name)
    Copy-Item -LiteralPath $Original -Destination (Join-Path $Sdk $Name) -Force
    if((Get-FileHash -LiteralPath $Original).Hash -ne (Get-FileHash -LiteralPath (Join-Path $Sdk $Name)).Hash){throw 'SDK copy mismatch'}
}
Copy-Item -LiteralPath $Source -Destination $Target -Force
New-Item -ItemType File -Path (Join-Path $Lab 'enable-game-sdk721.txt') -Force | Out-Null
[pscustomobject]@{installed_hash=(Get-FileHash -LiteralPath $Target).Hash;backup=$Backup;sdk=$Sdk} | ConvertTo-Json
