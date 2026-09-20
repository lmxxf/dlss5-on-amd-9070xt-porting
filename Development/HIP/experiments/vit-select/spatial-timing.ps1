$ErrorActionPreference='Stop';$r='D:\DLSSNR-Lab\hip-backend'
foreach($t in '0.15','0.5','1'){& "$r\packed-check.ps1" -Variant spatial -Phase Timing -Threshold $t}
