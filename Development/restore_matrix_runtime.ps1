$ErrorActionPreference='Stop'
$Lab='D:\DLSSNR-Lab'
$Game='C:\Program Files (x86)\Steam\steamapps\common\StellarBlade\SB\Binaries\Win64'
if(Get-Process SB-Win64-Shipping -ErrorAction SilentlyContinue){throw 'Exit game before restoring the baseline'}
$Backup=Join-Path $Lab 'matrix-probe\game-runtime-before-matrix.addon64'
if((Get-FileHash -LiteralPath $Backup).Hash -ne 'CA668B23D81DD06964053D13A03315FBA3BC04A920B93AC070C391058AF6B415'){throw 'Baseline backup hash mismatch'}
Copy-Item -LiteralPath $Backup -Destination (Join-Path $Game 'dlss5-1080p-runtime.addon64') -Force
foreach($Name in @('enable-game-sdk721.txt','enable-linalg-front-ffn.txt')){Remove-Item -LiteralPath (Join-Path $Lab $Name) -ErrorAction SilentlyContinue}
# The unused private SDK directory is retained; system DLLs and display driver are not changed.
Get-FileHash -LiteralPath (Join-Path $Game 'dlss5-1080p-runtime.addon64') | Format-List
