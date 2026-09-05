param([string]$Lab='D:\DLSSNR-Lab')
$ErrorActionPreference='Stop'
$Root=Join-Path $Lab 'matrix-probe'
$Exe=Join-Path $Root 'amd-preview-complete.exe'
$Expected='CCA61A1D5839B8B413B5DBA4D932AD16504CDC54F81DE475A6E08F7BA4FA08A2'
if((Get-Item -LiteralPath $Exe).Length -ne 874669800){throw 'Wrong package size'}
if((Get-FileHash -LiteralPath $Exe -Algorithm SHA256).Hash -ne $Expected){throw 'Package hash mismatch'}
$Signature=Get-AuthenticodeSignature -LiteralPath $Exe
$Result=[pscustomobject]@{status=$Signature.Status.ToString();signer=$Signature.SignerCertificate.Subject;thumbprint=$Signature.SignerCertificate.Thumbprint;sha256=$Expected}
$Result | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $Root 'preview-signature.json') -Encoding UTF8
$Result | ConvertTo-Json
if($Signature.Status -ne 'Valid' -or $Signature.SignerCertificate.Subject -notlike '*Advanced Micro Devices*'){throw 'AMD signature not verified'}
$Destination=Join-Path $Root 'preview-driver'
& (Join-Path $Root '7zr.exe') x $Exe ('-o'+$Destination) -y | Out-File -LiteralPath (Join-Path $Root 'driver-extract.log') -Encoding UTF8
if($LASTEXITCODE -ne 0){throw "Extraction returned $LASTEXITCODE; inspect driver-extract.log"}
Get-ChildItem -LiteralPath $Destination | Select-Object Name,Length
