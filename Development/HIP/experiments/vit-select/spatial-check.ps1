$ErrorActionPreference='Stop';$r='D:\DLSSNR-Lab\hip-backend'
& "$r\packed-check.ps1" -Variant spatial -Phase Control
& "$r\packed-check.ps1" -Variant spatial -Phase Sweep -SweepThresholds '0,0.15,0.5,1'
