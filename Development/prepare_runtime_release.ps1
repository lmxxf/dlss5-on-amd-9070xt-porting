param([string]$Lab='D:\DLSSNR-Lab')
$ErrorActionPreference='Stop'
$Game='C:\Program Files (x86)\Steam\steamapps\common\StellarBlade\SB\Binaries\Win64'
$Destination=Join-Path $Lab 'distribution\DLSS5-AMD-1080p'
[void][IO.Directory]::CreateDirectory($Destination)
Copy-Item -LiteralPath (Join-Path $Game 'd3d12.dll') -Destination (Join-Path $Destination 'd3d12.dll') -Force
Copy-Item -LiteralPath (Join-Path $Lab 'dlss5-1080p-runtime.addon64') -Destination $Destination -Force
Copy-Item -LiteralPath (Join-Path $Lab 'bundled-runtime-readme.txt') -Destination (Join-Path $Destination 'README.txt') -Force
foreach($Name in @('ReShade-LICENSE.txt','MinHook-LICENSE.txt')){Copy-Item -LiteralPath (Join-Path $Lab $Name) -Destination $Destination -Force}
$Hashes=Get-ChildItem -LiteralPath $Destination -File | Where-Object {$_.Name -ne 'SHA256SUMS.txt'} | ForEach-Object { $h=Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256; $h.Hash+'  '+$_.Name }
$Hashes | Set-Content -LiteralPath (Join-Path $Destination 'SHA256SUMS.txt') -Encoding ASCII
$Zip=Join-Path $Lab 'distribution\DLSS5-AMD-1080p.zip'
Compress-Archive -LiteralPath $Destination -DestinationPath $Zip -CompressionLevel Optimal -Force
Get-FileHash -LiteralPath $Zip -Algorithm SHA256 | Format-List
Get-Item -LiteralPath $Zip | Select-Object FullName,Length
