$ErrorActionPreference='Stop';$prefix='D:\DLSSNR-Lab\isa-asm-phase3-20261029\knife1M\'
for($i=0;$i -lt 30;$i++){
 $owned=@(Get-Process frame -ErrorAction SilentlyContinue|Where-Object {$_.Path.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)})
 if($owned.Count){foreach($p in $owned){Write-Output "STOP_OWN_MASK pid=$($p.Id) path=$($p.Path)";Stop-Process -Id $p.Id -Force};exit 0}
 Start-Sleep -Milliseconds 100
}
'NO_OWN_MASK_PROCESS'
