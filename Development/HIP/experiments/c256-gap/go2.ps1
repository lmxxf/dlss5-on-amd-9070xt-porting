# final candidate (build-final) vs current installed: lock -> 19 groups + rollover + 3 ABBA -> stress (timeout) -> unlock
$root='D:\DLSSNR-Lab\hip-backend\c256-gap-20261001'
if(Get-Process|Where-Object{$_.ProcessName -match 'Shipping|^re9$|^Onimusha|^SandFall'}){'GAME RUNNING';exit 1}
& "$root\lock.ps1" take;if($LASTEXITCODE){exit 1}
try{& "$root\run.ps1" -Name FIN -Builds final -Rounds 3 -NoBuild}finally{& "$root\lock.ps1" drop}
& "$root\stress.ps1" -Set FIN
'GO2_DONE'
