param([string]$TagPrefix='r2-temporal')
$ErrorActionPreference='Stop';$r='D:\DLSSNR-Lab\hip-backend'
foreach($mode in 2,1){$tag="$TagPrefix-$mode";& "$r\vit-adaptive-run.ps1" -Phase Quality -Tag $tag -CandidateMode $mode -Sequence 1 -Frames 12 -Temporal 1 -ResetEvery 4
 $base="$r\vit-adaptive-$tag-results\base-900-s1";$candidate="$r\vit-adaptive-$tag-results\adaptive-900-s1"
 if($mode -eq 2){foreach($i in 0..11){if((Get-FileHash "$base\rgb-frame-$i.f16").Hash -ne (Get-FileHash "$candidate\rgb-frame-$i.f16").Hash){throw "Temporal force-full mismatch $i"}}}
 $rows=@(Get-Content "$candidate\gate.csv"|ConvertFrom-Csv -Header frame,reuse,age,reason,global,local,image)
 foreach($i in 0,4,5,8,9){if([int]$rows[$i].reuse -ne 0){throw "History reset did not refresh: $i"}}
 Write-Output "TEMPORAL_PASS mode=$mode frames=12 reset_every=4"
}
