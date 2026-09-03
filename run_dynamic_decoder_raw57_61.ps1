param([int]$Frame)
$ErrorActionPreference='Stop';$L='D:\DLSSNR-Lab';$src="$L\raw-$Frame-block56.f32";$sh=@{57=0;58=1;59=3;60=2;61=0};foreach($b in 57..61){$out="$L\raw-$Frame-block$b.f32";& "$L\d3d12_block128_test.exe" "$L\block$b-body-effective.bin" $src $out 480 272 $sh[$b];if($LASTEXITCODE){throw "block$b"};$src=$out}
