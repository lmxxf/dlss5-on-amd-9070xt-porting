#!/usr/bin/env bash
set -euo pipefail
# Same generated host used by the benchmark; run build-host.sh before this script.
if [[ $# != 2 ]]; then echo 'usage: build-preview.sh MINHOOK_SOURCE RESHADE_INCLUDE' >&2; exit 2; fi
bash /tmp/vit-adaptive-src/scripts/build-addon.sh "$1" "$2" /tmp/native-vit-adaptive.addon64 --hip
