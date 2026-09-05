param([Parameter(Mandatory=$true)][ValidatePattern('^[0-9a-fA-F]{64}$')][string]$ExpectedHash)
$ErrorActionPreference='Stop'
$Lab='D:\DLSSNR-Lab'
$Game=Get-Process SB-Win64-Shipping -ErrorAction SilentlyContinue
if($Game){Stop-Process -InputObject $Game -Force;if(-not $Game.WaitForExit(30000)){throw 'Game did not exit'}}
foreach($Flag in @('enable-linalg-decoder-ffn.txt','enable-linalg-block70-ffn.txt')){
 $Path=Join-Path $Lab $Flag
 if(Test-Path $Path){Move-Item $Path ($Path+'.disabled') -Force}
}
Set-Content (Join-Path $Lab 'enable-linalg-front-ffn.txt') '4' -Encoding Ascii
& (Join-Path $Lab 'deploy_matrix_runtime.ps1') -ExpectedHash $ExpectedHash
Start-ScheduledTask -TaskName 'DLSS5Launch'
