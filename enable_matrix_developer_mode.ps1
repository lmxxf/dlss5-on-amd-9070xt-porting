param([string]$Lab='D:\DLSSNR-Lab')
$ErrorActionPreference='Stop'
$Key='HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\AppModelUnlock'
$Name='AllowDevelopmentWithoutDevLicense'
$Dir=Join-Path $Lab 'matrix-probe'
[void][IO.Directory]::CreateDirectory($Dir)
$Backup=Join-Path $Dir 'developer-mode-before.json'
if(-not(Test-Path -LiteralPath $Backup)){
    $Item=Get-ItemProperty -LiteralPath $Key -ErrorAction SilentlyContinue
    $Property=if($Item){$Item.PSObject.Properties[$Name]}else{$null}
    [pscustomobject]@{key_existed=(Test-Path -LiteralPath $Key);value_existed=($null -ne $Property);value=if($Property){$Property.Value}else{$null}} | ConvertTo-Json | Set-Content -LiteralPath $Backup -Encoding UTF8
}
if(-not(Test-Path -LiteralPath $Key)){New-Item -Path $Key -Force | Out-Null}
New-ItemProperty -LiteralPath $Key -Name $Name -PropertyType DWord -Value 1 -Force | Out-Null
[pscustomobject]@{developer_mode=(Get-ItemProperty -LiteralPath $Key).$Name;backup=$Backup} | ConvertTo-Json
