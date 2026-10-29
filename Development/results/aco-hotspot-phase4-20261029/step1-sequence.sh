#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
r='D:\DLSSNR-Lab\aco-hotspot-phase4-20261029'
ssh amd9070 "powershell -NoProfile -ExecutionPolicy Bypass -File $r\\run-jobs.ps1 -Manifest $r\\step1-split.json"
ssh amd9070 "powershell -NoProfile -ExecutionPolicy Bypass -File $r\\run-jobs.ps1 -Manifest $r\\step1-nmmn.json"
for case in step1-cold step1-split step1-nmmn;do
 ssh amd9070 "powershell -NoProfile -ExecutionPolicy Bypass -File $r\\collect.ps1 -Case $case"
 scp "amd9070:D:/DLSSNR-Lab/aco-hotspot-phase4-20261029/$case-evidence.zip" Development/results/aco-hotspot-phase4-20261029/
 python3 Development/results/aco-hotspot-phase4-20261029/collect-local.py "$case"
done
