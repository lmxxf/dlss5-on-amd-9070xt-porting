$ErrorActionPreference='Stop';$r='D:\DLSSNR-Lab\release-046';$held=$null
function Idle {& D:\DLSSNR-Lab\game-check.ps1 'Shipping SB-Win64 Onimusha re9.exe SandFall Magpie';if($LASTEXITCODE -ne 1){throw 'game busy'};foreach($d in 'C','D'){if((Get-PSDrive $d).Free -lt 100GB){throw 'disk below100GB'}}}
try{
 Idle;$held=[IO.File]::Open('D:\DLSSNR-Lab\gpu.lock',[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None);Idle
 $out='D:\給網友打包';New-Item -ItemType Directory "$out\history" -Force|Out-Null
 $history=@(Get-ChildItem "$out\history" -File|Where-Object Name -match '0.44'|ForEach-Object {@{path=$_.FullName;sha256=(Get-FileHash $_.FullName).Hash}})
 foreach($x in (Get-Content "$r\baseline-045.json" -Raw -Encoding UTF8|ConvertFrom-Json).packages){
  $n=$x.name+'.zip';$src="$out\$n";$dst="$out\history\$n";if(!(Test-Path $src)){$src=$dst}
  if((Get-FileHash $src).Hash -ne $x.sha256){throw '045 baseline ZIP mismatch'}
  if(((Get-Content "$src.sha256" -Raw).Trim() -split '\s+')[0] -ne $x.sha256){throw '045 sidecar mismatch'}
  if($src -ne $dst){if(Test-Path $dst){throw 'history collision'};Move-Item $src $dst;Move-Item "$src.sha256" "$dst.sha256"}
  if((Get-FileHash $dst).Hash -ne $x.sha256){throw 'history readback mismatch'}
 }
 $history|ConvertTo-Json|Set-Content "$r\history044-before.json"
 foreach($x in $history){if((Get-FileHash $x.path).Hash -ne $x.sha256){throw '044 drift'}}
 Expand-Archive "$r\config045.zip" "$r\scripts";Expand-Archive "$r\source045.zip" "$r\source"
 foreach($f in (Get-Content "$r\payload-expected.json" -Raw -Encoding UTF8|ConvertFrom-Json)){if((Get-FileHash (Join-Path $r $f.relative)).Hash -ne $f.sha256){throw 'payload drift'}}
 & "$r\package-046.ps1" -SourceCommit '2f19eebc' -ConfigDirectory "$r\scripts"
 if(!$?){throw 'package failed'}
 foreach($x in $history){if((Get-FileHash $x.path).Hash -ne $x.sha256){throw '044 drift'}}
 'PACKAGES_COMPLETE'
}finally{if($held){$held.Dispose();Remove-Item D:\DLSSNR-Lab\gpu.lock}}
