$ErrorActionPreference='Stop';$r='D:\DLSSNR-Lab\c32-stage-phase5-20261029';$p4='D:\DLSSNR-Lab\aco-hotspot-phase4-20261029'
Copy-Item "$p4\mochi-tree-before.json" "$r\mochi-tree-before.json"
Copy-Item "$p4\native-assets-before.json" "$r\native-assets-before.json"
New-Item -ItemType Directory -Force "$r\aco-cache"|Out-Null
