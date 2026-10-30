#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
p=Development/results/ksearch-step3-20261030
r='D:\DLSSNR-Lab\ksearch-step3-20261030'
role=${1:?role};gold=${2:-yes}
if [[ "$gold" == yes ]];then ssh amd9070 "powershell -NoProfile -ExecutionPolicy Bypass -File $r\\run-jobs.ps1 -Manifest $r\\$role-gold.json";fi
ssh amd9070 "powershell -NoProfile -ExecutionPolicy Bypass -File $r\\run-jobs.ps1 -Manifest $r\\$role-kernel.json"
ssh amd9070 "powershell -NoProfile -ExecutionPolicy Bypass -File $r\\run-jobs.ps1 -Manifest $r\\$role-perf.json"
ssh amd9070 "powershell -NoProfile -ExecutionPolicy Bypass -File $r\\collect.ps1 -Case $role"
scp "amd9070:D:/DLSSNR-Lab/ksearch-step3-20261030/$role-evidence.zip" "$p/"
python3 "$p/collect-local.py" "$role"
python3 "$p/analyze.py" "$p/$role/evidence" "$p/$role-statistics.json" > "$p/$role-statistics.txt"
