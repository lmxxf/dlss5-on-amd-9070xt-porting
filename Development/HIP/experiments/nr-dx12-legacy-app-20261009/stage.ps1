param([switch]$Stage)
$ErrorActionPreference='Stop';$r='D:\DLSSNR-Lab\nr-dx12-legacy-app-20261009'
# This script stages only; it does not create a D3D device or execute the benchmark.
if(!$Stage){'PLAN_ONLY: copy existing legacy full71 assets/CSO to private directory; overlay five current codec/input shaders; copy existingAgility721';return}
if([IO.DriveInfo]::new('D:\').AvailableFreeSpace -lt 100GB){throw 'D low'}
New-Item -ItemType Directory -Force "$r\assets","$r\D3D12"|Out-Null
Get-ChildItem 'D:\DLSSNR-Lab\native-game-tiled-assets' -File|ForEach-Object{Copy-Item $_.FullName "$r\assets" -Force}
if(!(Test-Path "$r\assets\noise.f32") -and !(Test-Path "$r\assets\noise.f16")){if(Test-Path 'D:\DLSSNR-Lab\history-trial-041a-20261006\assets\noise.f32'){Copy-Item 'D:\DLSSNR-Lab\history-trial-041a-20261006\assets\noise.f32' "$r\assets\noise.f32"}else{Copy-Item 'D:\DLSSNR-Lab\history-trial-041a-20261006\assets\noise.f16' "$r\assets\noise.f16"}}
Get-ChildItem "$r\codec-current" -File|ForEach-Object{Copy-Item $_.FullName "$r\assets" -Force}
Copy-Item 'D:\DLSSNR-Lab\matrix-probe\D3D12\*' "$r\D3D12" -Force
Get-ChildItem "$r\assets" -File|ForEach-Object{[pscustomobject]@{name=$_.Name;bytes=$_.Length;sha256=(Get-FileHash $_.FullName).Hash}}|ConvertTo-Json|Set-Content "$r\staged-manifest.json"
'STAGE_ONLY_PASS NO_GPU'
