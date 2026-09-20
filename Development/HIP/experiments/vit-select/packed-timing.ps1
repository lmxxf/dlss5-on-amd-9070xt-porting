$ErrorActionPreference='Stop'
foreach($t in '-1','0.5','1'){
 & 'D:\DLSSNR-Lab\hip-backend\packed-check.ps1' -Phase Timing -Threshold $t
}
