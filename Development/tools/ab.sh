#!/bin/bash
# usage: ab.sh <name> <runner.ps1> [files to push...]   (clones D:\DLSSNR-Lab\$AB_BASE, default epi, when <name> does not exist)
set -e
NAME=$1;RUNNER=$2;shift 2
R="D:\\DLSSNR-Lab\\$NAME"
ssh amd9070 "if not exist $R robocopy D:\\DLSSNR-Lab\\${AB_BASE:-epi} $R /E /XF network.*.log run.json gpu-network70*.f32 /NFL /NDL /NJH >nul" || true
for f in "$@"; do scp -q "$f" amd9070:"D:/DLSSNR-Lab/$NAME/"; done
ssh amd9070 "set DLSS5_TEST_FRAME_COUNT=15&& powershell -ExecutionPolicy Bypass -File $R\\$RUNNER -Folder $R > $R\\driver.log 2>&1"
mkdir -p release/$NAME
scp -q amd9070:"D:/DLSSNR-Lab/$NAME/network.stdout.log" amd9070:"D:/DLSSNR-Lab/$NAME/gpu-network70.f32" amd9070:"D:/DLSSNR-Lab/$NAME/gpu-network70-temporal.f32" release/$NAME/
ssh amd9070 "type $R\\network.stderr.log" | grep -iv "warning\|^ \|\^\|^native_\|^$" | tail -3 || true
python3 tools/compare_fast_output.py --root release/$NAME
