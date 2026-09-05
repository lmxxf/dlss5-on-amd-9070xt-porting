param([string]$Lab='D:\DLSSNR-Lab')
$ErrorActionPreference='Stop'
$Driver=Get-CimInstance Win32_PnPSignedDriver | Where-Object {$_.DeviceName -eq 'AMD Radeon RX 9070 XT'}
if(@($Driver).Count -ne 1 -or $Driver.DriverProviderName -notlike '*Advanced Micro Devices*'){throw 'AMD target not uniquely identified'}
$Destination=Join-Path $Lab ('matrix-probe\driver-backup\'+$Driver.DriverVersion)
[void][IO.Directory]::CreateDirectory($Destination)
$Driver | Select-Object DeviceName,DriverVersion,InfName,DeviceID,DriverProviderName | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $Destination 'before.json') -Encoding UTF8
& pnputil.exe /export-driver $Driver.InfName $Destination
if($LASTEXITCODE -ne 0){throw "Driver export failed: $LASTEXITCODE"}
$Files=@(Get-ChildItem -LiteralPath $Destination -Recurse -File)
if(-not($Files | Where-Object Extension -eq '.inf')){throw 'Export contains no INF'}
[pscustomobject]@{backup=$Destination;file_count=$Files.Count;bytes=($Files | Measure-Object Length -Sum).Sum;free_bytes=(Get-PSDrive D).Free} | ConvertTo-Json
