$ErrorActionPreference='Stop'
if(Get-Process native-c64-multi-bench -ErrorAction SilentlyContinue){throw 'Benchmark already running; inspect it first'}
$env:DLSS5_BENCH_LEGACY_SHADER_DIR='D:\DLSSNR-Lab\native-network70-profile'
foreach($Channels in 128,256){
 $Folder="D:\DLSSNR-Lab\c64-tiled-raw$Channels"
 & "$Folder\native-c64-multi-bench.exe" $Folder tiled_full detail *> "$Folder\result.log"
 if($LASTEXITCODE -ne 0){throw "Raw C$Channels failed: $LASTEXITCODE"}
 Write-Output "Raw C$Channels passed"
}
