$ErrorActionPreference='Stop';$p='D:\DLSSNR-Lab\AttExp-preview';$game=Join-Path $p ('install-test-'+[guid]::NewGuid().ToString('N'));$oldState=if(Test-Path "$p\last-backup.txt"){[IO.File]::ReadAllBytes("$p\last-backup.txt")}else{$null}
New-Item -ItemType Directory -Force "$game\DLSS5-AMD\native-game-tiled-assets\HIP\gfx1201"|Out-Null
[IO.File]::WriteAllText("$game\dlss5-amd.addon64",'dummy old DLL')
[IO.File]::WriteAllText("$game\DLSS5-AMD\native-game-tiled-assets\HIP\gfx1201\deep_fast-packed.hsaco",'dummy old module')
[IO.File]::WriteAllText("$game\DLSS5-AMD\native-game-flags.txt","DLSS5_SHOW_FPS=0`nDLSS5_NETWORK_HEIGHT=900`nDLSS5_HIP_MODULES=old-location`n")
$beforeDirs=@(Get-ChildItem $game -Recurse -Directory|ForEach-Object{$_.FullName}|Sort-Object)
$before=@(Get-ChildItem $game -Recurse -File|ForEach-Object{[pscustomobject]@{path=$_.FullName;hash=(Get-FileHash $_.FullName).Hash}})
try{
 & "$p\install-preview.ps1" -Action Install -GameDir $game
 $firstBackup=Get-Content "$p\last-backup.txt" -Raw
 & "$p\install-preview.ps1" -Action Install -GameDir $game
 if((Get-Content "$p\last-backup.txt" -Raw) -ne $firstBackup){throw 'Repeat install replaced the original backup'}
 if(Test-Path "$game\DLSS5-AMD\HIP"){throw 'Installed modules outside the runtime asset directory'}
 if(@(Get-ChildItem "$game\DLSS5-AMD\native-game-tiled-assets\HIP" -Recurse -Filter *.hsaco).Count -ne 48){throw 'Install module count'}
 $flags=Get-Content "$game\DLSS5-AMD\native-game-flags.txt" -Raw
 if($flags -notmatch 'DLSS5_SHOW_FPS=0' -or $flags -match 'old-location' -or $flags -notmatch 'DLSS5_VIT_ADAPTIVE=1'){throw 'Flags preserve/override failed'}
 & "$p\install-preview.ps1" -Action Restore -GameDir $game
 $after=@(Get-ChildItem $game -Recurse -File);if($after.Count -ne $before.Count){throw 'Restore left candidate files'}
 foreach($f in $before){if((Get-FileHash $f.path).Hash -ne $f.hash){throw 'Restored original differs'}}
 if(Compare-Object $beforeDirs @(Get-ChildItem $game -Recurse -Directory|ForEach-Object{$_.FullName}|Sort-Object)){throw 'Restore left new architecture directories'}
 Write-Output 'PASS: isolated install and restore, 50 payload hashes, previous files/config restored exactly.'
}finally{if($null -ne $oldState){[IO.File]::WriteAllBytes("$p\last-backup.txt",$oldState)}else{if(Test-Path "$p\last-backup.txt"){Remove-Item "$p\last-backup.txt"}}}
