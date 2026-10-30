$ErrorActionPreference='Stop';$r='D:\DLSSNR-Lab\ksearch-step3-20261030';$p4='D:\DLSSNR-Lab\aco-hotspot-phase4-20261029'
Copy-Item "$p4\mochi-tree-before.json" "$r\mochi-tree-before.json"
Copy-Item "$p4\native-assets-before.json" "$r\native-assets-before.json"
New-Item -ItemType Directory -Force "$r\aco-cache"|Out-Null
