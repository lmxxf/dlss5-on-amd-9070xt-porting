#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
p=Development/results/aco-hotspot-phase4-20261029
r='D:\DLSSNR-Lab\aco-hotspot-phase4-20261029'
ssh amd9070 "powershell -NoProfile -ExecutionPolicy Bypass -File $r\\run-jobs.ps1 -Manifest $r\\3b-gold.json"
for role in 3a 3b;do
 ssh amd9070 "powershell -NoProfile -ExecutionPolicy Bypass -File $r\\run-jobs.ps1 -Manifest $r\\$role-kernel.json"
 ssh amd9070 "powershell -NoProfile -ExecutionPolicy Bypass -File $r\\run-jobs.ps1 -Manifest $r\\$role-perf.json"
 ssh amd9070 "powershell -NoProfile -ExecutionPolicy Bypass -File $r\\collect.ps1 -Case $role"
 scp "amd9070:D:/DLSSNR-Lab/aco-hotspot-phase4-20261029/$role-evidence.zip" "$p/"
 python3 "$p/collect-local.py" "$role"
 python3 "$p/analyze.py" "$p/$role/evidence" "$p/$role-statistics.json" > "$p/$role-statistics.txt"
done
