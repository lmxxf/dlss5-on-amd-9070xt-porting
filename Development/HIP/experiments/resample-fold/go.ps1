# lock -> run.ps1 (mkset + 19-group bit-exact + rollover + ABBA rounds) -> unlock
param([string]$Name='DF',[int]$Rounds=3)
$root='D:\DLSSNR-Lab\hip-backend\resample-fold-20261001'
if(Get-Process|Where-Object{$_.ProcessName -match 'Shipping|^re9$|^Onimusha|^SandFall'}){'GAME RUNNING';exit 1}
& "$root\lock.ps1" take;if($LASTEXITCODE){exit 1}
try{& "$root\run.ps1" -Name $Name -Rounds $Rounds -NoBuild}finally{& "$root\lock.ps1" drop}
'GO_DONE'
