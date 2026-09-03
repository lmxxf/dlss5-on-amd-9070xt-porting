param([int]$Frame)
$ErrorActionPreference='Stop';$L='D:\DLSSNR-Lab';$src="$L\raw-$Frame-block39.f32";$sh=@{40=0;41=1;42=3;43=2;44=0;45=1;46=3;47=2};foreach($b in 40..47){$out="$L\raw-$Frame-block$b.f32";& "$L\d3d12_block128_test.exe" "$L\block$b-logical-effective.bin" $src $out 120 72 $sh[$b];if($LASTEXITCODE){throw "block$b"};$src=$out}
