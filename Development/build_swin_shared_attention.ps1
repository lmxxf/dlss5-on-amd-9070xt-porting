param([string]$Lab='D:\DLSSNR-Lab')
$ErrorActionPreference='Stop'
$Dxc=Join-Path $Lab 'dxc\bin\x64\dxc.exe'
foreach($Shape in @(@(480,272,2),@(240,136,4),@(120,72,8),@(64,40,16))){
    $W=$Shape[0];$H=$Shape[1];$Heads=$Shape[2]
    foreach($Shift in 0..3){
        $X=[int]($Shift -eq 1 -or $Shift -eq 2)
        $Y=[int]($Shift -eq 1 -or $Shift -eq 3)
        $Output=Join-Path $Lab "shader-cache\swin-shared-$W-$H-$Heads-$Shift.cso"
        & $Dxc -T cs_6_2 -E main -O3 -D "WIDTH=$W" -D "HEIGHT=$H" -D "HEADS=$Heads" -D "SHIFT_X=$X" -D "SHIFT_Y=$Y" (Join-Path $Lab 'swin_attention_shared.hlsl') -Fo $Output
        if($LASTEXITCODE){throw "Compile failed: $Output"}
    }
}
