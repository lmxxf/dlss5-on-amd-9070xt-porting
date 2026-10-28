#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
p=Development/results/isa-asm-phase3-20261029
remote='D:\DLSSNR-Lab\isa-asm-phase3-20261029'
ssh amd9070 "powershell -NoProfile -ExecutionPolicy Bypass -File $remote\\run-jobs.ps1 -Manifest $remote\\final-perf.json"
ssh amd9070 "powershell -NoProfile -ExecutionPolicy Bypass -File $remote\\run-jobs.ps1 -Manifest $remote\\smoke.json"
ssh amd9070 "powershell -NoProfile -ExecutionPolicy Bypass -File $remote\\setup-gap.ps1"
ssh amd9070 "powershell -NoProfile -ExecutionPolicy Bypass -File $remote\\run-jobs.ps1 -Manifest $remote\\gap-jobs.json"
ssh amd9070 "powershell -NoProfile -ExecutionPolicy Bypass -File $remote\\verify-gap.ps1"
for case in full full-fix final smoke gap;do
 ssh amd9070 "powershell -NoProfile -ExecutionPolicy Bypass -File $remote\\collect.ps1 -Case $case"
 scp "amd9070:D:/DLSSNR-Lab/isa-asm-phase3-20261029/$case-evidence.zip" "$p/"
 python3 "$p/collect-local.py" "$case"
done
python3 "$p/analyze.py" "$p/final/evidence" "$p/final-statistics.json" > "$p/final-statistics.txt"
python3 "$p/analyze-gap.py" > "$p/gap-statistics.txt"
scp 'amd9070:D:/DLSSNR-Lab/isa-asm-phase3-20261029/mochi-tree-*.json' 'amd9070:D:/DLSSNR-Lab/isa-asm-phase3-20261029/native-assets-*.json' "$p/"
