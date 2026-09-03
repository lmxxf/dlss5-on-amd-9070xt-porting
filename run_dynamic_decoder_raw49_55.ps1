param([int]$Frame)
$ErrorActionPreference='Stop';$L='D:\DLSSNR-Lab';$src="$L\raw-$Frame-block48.f32";$sh=@{49=0;50=1;51=3;52=2;53=0;54=1;55=3};foreach($b in 49..55){$out="$L\raw-$Frame-block$b.f32";& "$L\d3d12_block128_test.exe" "$L\block$b-body-effective.bin" $src $out 240 136 $sh[$b];if($LASTEXITCODE){throw "block$b"};$src=$out}
