$ErrorActionPreference='Stop'
$r='D:\DLSSNR-Lab\nr-dx12-matrix-probe-20261009';$dxc='D:\DLSSNR-Lab\matrix-probe\dxc-preview'
Start-Transcript "$r\cpu-build.log"
try {
Get-Item "$dxc\bin\x64\dxc.exe","$dxc\bin\x64\dxcompiler.dll","D:\DLSSNR-Lab\matrix-probe\D3D12\D3D12Core.dll" | ForEach-Object {"VERSION $($_.FullName) $($_.VersionInfo.FileVersion)";Get-FileHash $_.FullName}
Get-FileHash "$dxc\inc\hlsl\dx\linalg.h"
New-Item -ItemType Directory -Force "$r\D3D12"|Out-Null
Copy-Item 'D:\DLSSNR-Lab\matrix-probe\D3D12\D3D12Core.dll' "$r\D3D12\D3D12Core.dll"
foreach($type in @('fp8','f16')) {
 & "$dxc\bin\x64\dxc.exe" -I "$dxc\inc\hlsl" -T cs_6_10 -E main -HV 2021 -enable-16bit-types -O3 "$r\probe-$type.hlsl" -Fo "$r\probe-$type.cso"
 if($LASTEXITCODE){throw "DXC $type failed $LASTEXITCODE"}
 Get-FileHash "$r\probe-$type.hlsl","$r\probe-$type.cso"
}
'CPU_BUILD_OK_NOT_GPU_SUPPORT'
} finally {Stop-Transcript}
