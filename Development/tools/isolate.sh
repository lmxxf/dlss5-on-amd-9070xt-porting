#!/bin/bash
# usage: isolate.sh <name> <runner.ps1> <isolate list> [repeat]  -- no rebuild; prints network_isolate lines
set -e
NAME=$1;RUNNER=$2;LIST=$3;REP=${4:-200};R="D:\\DLSSNR-Lab\\$NAME"
ssh amd9070 "set DLSS5_TEST_FRAME_COUNT=3&& set DLSS5_TEST_ISOLATE=$LIST&& set DLSS5_TEST_ISOLATE_REPEAT=$REP&& powershell -ExecutionPolicy Bypass -File $R\\bench-norebuild.ps1 -Folder $R > $R\\driver.log 2>&1" || true
ssh amd9070 "type $R\\network.stdout.log" | grep -a "network_isolate" | sed 's/.*stage=\([^ ]*\).*per_ms=\([0-9.]*\).*/\1 \2/'
