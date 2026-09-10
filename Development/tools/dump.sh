#!/bin/bash
# usage: dump.sh <name> <runner.ps1> <DUMP_BLOCK4 code> <remote file> [extra env "A=1&& set B=2"]  -- rerun, fetch one dump to release/<name>/
set -e
NAME=$1;RUNNER=$2;CODE=$3;FILE=$4;EXTRA=${5:-};R="D:\\DLSSNR-Lab\\$NAME"
[ -n "$EXTRA" ] && EXTRA="set $EXTRA&& "
ssh amd9070 "${EXTRA}set DLSS5_TEST_FRAME_COUNT=3&& set DLSS5_TEST_DUMP_BLOCK4=$CODE&& powershell -ExecutionPolicy Bypass -File $R\\$RUNNER -Folder $R > $R\\driver.log 2>&1"
mkdir -p release/$NAME;scp -q amd9070:"D:/DLSSNR-Lab/$NAME/$FILE" release/$NAME/
