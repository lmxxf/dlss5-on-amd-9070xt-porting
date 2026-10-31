$ErrorActionPreference='Stop'
$r='D:\DLSSNR-Lab\yami-round5-20261031'
if(Test-Path 'D:\DLSSNR-Lab\gpu.lock'){throw 'wait for GPU jobs to end'}
$src='D:\DLSSNR-Lab\ksearch-round4-20261031\payload'
$dst="$r\unchanged-hosts";New-Item -ItemType Directory -Force $dst|Out-Null
$rows=@();foreach($name in @('frame-standard.exe','frame-window.exe','pure.exe','final-profile.exe','LmxxfNrRuntime.dll','dlss5-amd.addon64')){
 Copy-Item "$src\$name" "$dst\$name"; $rows+=@{name=$name;sha256=(Get-FileHash "$dst\$name" -Algorithm SHA256).Hash.ToLower()}
}
$rows|ConvertTo-Json|Set-Content "$r\unchanged-hosts.json"
Compress-Archive -Path "$dst\*" -DestinationPath "$r\unchanged-hosts.zip" -Force
Get-Content "$r\unchanged-hosts.json"
