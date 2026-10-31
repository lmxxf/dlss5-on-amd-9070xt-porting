param([string]$SelectedConfig='')
$ErrorActionPreference='Stop'
$r='D:\DLSSNR-Lab\yami-round5-20261031'
if(Test-Path 'D:\DLSSNR-Lab\gpu.lock'){throw 'GPU lock still present'}
$files=@{};$names=@('final-five-long')
if($SelectedConfig){$names+=$SelectedConfig}
foreach($name in $names){$m=Get-Content "$r\$name.json" -Raw|ConvertFrom-Json;foreach($f in $m.files){if($files.ContainsKey($f.path) -and $files[$f.path] -ne $f.sha256){throw "conflicting hash $($f.path)"};$files[$f.path]=$f.sha256}}
$bad=@();foreach($path in $files.Keys){if((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLower() -ne $files[$path].ToLower()){$bad+=$path}}
if($bad.Count){throw ('SHA drift '+($bad -join ', '))}
$own=@(Get-Process|Where-Object {try{$_.Path -like "$r\*"}catch{$false}}|Select-Object Id,ProcessName,Path)
$related=@(Get-Process|Where-Object{$_.ProcessName -match '^(frame|pure|final-profile|rtc_compile-current|occupancy|guarded-dot2-probe|guarded-reductions-probe)$'}|Select-Object Id,ProcessName,Path)
@{machine=$env:COMPUTERNAME;utc=[DateTime]::UtcNow.ToString('o');gpu_lock_present=(Test-Path 'D:\DLSSNR-Lab\gpu.lock');own_processes=$own;related_processes=$related;verified_files=$files.Count;hash_mismatches=$bad;d_free_gib=[Math]::Round((Get-PSDrive D).Free/1GB,3)}|ConvertTo-Json -Depth 6|Set-Content "$r\final-status.json"
Get-Content "$r\final-status.json"
