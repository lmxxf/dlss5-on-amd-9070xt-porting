#!/bin/bash
S=$(dirname "$0"); R='D:\DLSSNR-Lab\down-only'; cd /home/lmxxf/work/ai-theorys-study/wechat/assets/297
: > $S/sweep.log
for b in 6 7 10 11 12 13 16 17 18 19 20 21 23 24 25 26 27 28 29 30 31 32 33 34 35 36 37 38 40 41 42 43 44 45 46 47 50 51 52 53 54 58 59 60 64; do
 ssh amd9070 "set DLSS5_TEST_FRAME_COUNT=3&& set DLSS5_SKIP_BLOCKS=$b&& powershell -ExecutionPolicy Bypass -File $R\\bench-norebuild.ps1 -Folder $R > $R\\driver.log 2>&1"
 err=$(ssh amd9070 "type $R\\driver.log" | grep -i "skip block\|exception\|error" | head -1)
 rm -f release/flat/*; scp -q amd9070:"D:/DLSSNR-Lab/down-only/gpu-network70.f32" amd9070:"D:/DLSSNR-Lab/down-only/gpu-network70-temporal.f32" amd9070:"D:/DLSSNR-Lab/down-only/network.stdout.log" release/flat/ 2>/dev/null
 psnr=$(python3 tools/compare_fast_output.py --root release/flat 2>/dev/null | head -1 | grep -o "psnr_db': [0-9.]*" | cut -d' ' -f2)
 echo "block=$b psnr=$psnr $err" >> $S/sweep.log
done
echo DONE >> $S/sweep.log
