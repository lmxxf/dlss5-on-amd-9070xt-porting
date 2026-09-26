# Dry run of deploy\install.ps1 on the lab "old" tree (same layout as the RE9 package): install, check, run, restore, check.
$ErrorActionPreference='Stop'
$lab='D:\DLSSNR-Lab\re9-runtime-flags-20260926';$g="$lab\old";$d="$lab\deploy"
function State{ "runtime=" + (Get-FileHash "$g\LmxxfNrRuntime.dll").Hash.Substring(0,8) + " c32w1=" + (Get-FileHash "$g\DLSS5-AMD\native-game-tiled-assets\HIP\gfx1201\c32-wave1.hsaco").Hash.Substring(0,8) + " sums=" + (Get-FileHash "$g\DLSS5-AMD\native-game-tiled-assets\HIP\SHA256SUMS").Hash.Substring(0,8) + " flags=" + (Get-FileHash "$g\DLSS5-AMD\native-game-flags.txt").Hash.Substring(0,8) }
'before  ' + (State)
$r=& "$d\install.ps1" -GameDir $g; $r
'after   ' + (State)
(Select-String -Path "$g\DLSS5-AMD\native-game-flags.txt" -Pattern 'NETWORK_HEIGHT|WAVE_OWNED|C512_M32|PROJ_N64|HIP_PDL').Line -join ' ; '
& "$lab\in\rt_bench.exe" "$g\LmxxfNrRuntime.dll" "$g\DLSS5-AMD\native-game-tiled-assets\HIP" 1920x1080 24 1 2>&1 | Select-Object -First 1
$bk=($r -split 'BACKUP=')[1]
& "$d\install.ps1" -GameDir $g -RestoreBackup $bk
'restored ' + (State)
