$ErrorActionPreference='Stop'
function Idle {if(Get-Process SB-Win64-Shipping,Magpie,re9,LOP-Win64-Shipping -ErrorAction SilentlyContinue){throw 'Game/Magpie running'}}
Idle;Set-Location D:\DLSSNR-Lab\hip-backend\sparse-ffn-study
& .\bench.exe
if($LASTEXITCODE){throw 'Sparse probe failed'};Idle
