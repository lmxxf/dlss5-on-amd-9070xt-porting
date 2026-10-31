$ErrorActionPreference='Stop'
$r='D:\DLSSNR-Lab\yami-round5-20261031'
& "$r\run-jobs.ps1" -Manifest "$r\c64-one-plane-perf.json"
& "$r\run-jobs.ps1" -Manifest "$r\c256-one-plane-rc-perf.json"
